// -----------------------------------------------------------------------------
// Camper Control - SenseCAP Indicator D1
//
// One cooperative loop: integrations get a slice, then the UI, then LVGL. No
// integration is allowed to block for long, which is why the BLE client and the
// BMS parameter reads were reworked during the port. If something ever has to
// block, give it its own FreeRTOS task and let it publish through entities.
// -----------------------------------------------------------------------------
#include <Arduino.h>
#include <lvgl.h>

#include "board_indicator_d1.h"
#include "bsp/display.h"
#include "bsp/storage.h"
#include "core/alarms.h"
#include "core/clock.h"
#include "core/integration.h"
#include "core/settings.h"
#include "integrations/ble_broadcast.h"
#include "integrations/lighting.h"
#include "integrations/bms_jbd.h"
#include "integrations/env_sensors.h"
#include "integrations/system_info.h"
#include "ui/ui.h"

namespace {

// Front panel button: wake the screen, and silence an active alarm.
void serviceButton() {
  static bool wasDown = false;
  const bool down = digitalRead(PIN_USER_BUTTON) == LOW;
  if (down && !wasDown) {
    bsp::wakeBacklight();
    if (cc::Alarms::instance().any()) cc::Alarms::instance().silence();
  }
  wasDown = down;
}

// The audible alarm is not wired yet - the Indicator's buzzer sits on the RP2040
// side of the board, so it needs the UART link before it can sound. Until then
// the alarm is visual only, and this is the single place to hook the buzzer in.
void serviceAlarmOutput() {
  static bool sounding = false;
  const bool want = cc::Alarms::instance().shouldSound();
  if (want != sounding) {
    sounding = want;
    log_w("audible alarm would be %s", want ? "ON" : "OFF");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);  // give USB CDC a moment so the boot log is not lost
  log_i("Camper Control starting");

  pinMode(PIN_USER_BUTTON, INPUT_PULLUP);

  // Before the display, which reads the stored brightness, and before the BMS,
  // which reads the stored address.
#if CFG_LOG_TO_FLASH
  // Before the display, and that is load-bearing rather than tidy: mounting
  // reads flash, and once the RGB panel is refreshing any flash access can
  // panic the chip (decisions/0014). This is the one safe window.
  bsp::storageBegin();
#endif

  cc::Settings::instance().begin();

  if (!bsp::displayBegin()) {
    log_e("display failed to start - continuing headless");
  }

  cc::Hub& hub = cc::Hub::instance();
  hub.add(&cc::bms());
  hub.add(&cc::envSensors());
#if CFG_LIGHTING
  // After the BMS, which brings NimBLE up, and before the broadcaster, which
  // only reads. This one owns a task and a connection slot.
  hub.add(&cc::lighting());
#endif
#if CFG_BLE_BROADCAST
  // Last of the radio users, and the order is load-bearing: this one reads the
  // registry rather than filling it, and it needs NimBLE already up - which
  // the BMS integration's begin() does. It stands NimBLE up itself if the BMS
  // is ever disabled, but this way it never has to.
  hub.add(&cc::bleBroadcast());
#endif
  // Future accessories land here: relay nodes, tank senders, the heater.
  //
  // Above the next line, not below it. systemInfo() walks the hub in its own
  // begin() to give everything already added a link row on the System screen,
  // so an integration registered after it gets no row.
  hub.add(&cc::systemInfo());
  hub.begin();

  cc::ui::begin();

  // The UI is built once and kept, so this number is the whole cost of every
  // screen in the project and it only moves when a screen is added. It stays
  // because exhausting this pool does not report itself: LVGL's arc mask
  // lv_malloc()s at draw time, does not check the result, and the board boot
  // loops in a stack trace that names no file of ours. See
  // docs/TROUBLESHOOTING.md.
  {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    log_i("lvgl pool: free %u largest %u used %u%% frag %u%%",
          (unsigned)mon.free_size, (unsigned)mon.free_biggest_size,
          (unsigned)mon.used_pct, (unsigned)mon.frag_pct);
  }
  log_i("boot complete, free heap %u", (unsigned)ESP.getFreeHeap());
}

void loop() {
  // First, so everything downstream sees one consistent idea of the time.
  //
  // Gated on a counter, not run every pass: millis() is a 64-bit division and
  // this loop turns over ~22,000 times a second. Every 256 iterations is still
  // ~85 Hz against a clock that moves once a second, and Clock::tick() keeps
  // the sub-second remainder so a coarse call rate costs no accuracy at all.
  static uint16_t clockTick = 0;
  if ((++clockTick & 0xFF) == 0) cc::Clock::instance().tick(millis());
  cc::Settings::instance().loop();  // commits anything an integration handed over
  cc::Hub::instance().loop();
  serviceButton();
  serviceAlarmOutput();
  cc::ui::tick();
  bsp::displayLoop();
}
