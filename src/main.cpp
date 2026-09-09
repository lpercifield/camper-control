// -----------------------------------------------------------------------------
// Camper Control - SenseCAP Indicator D1
//
// One cooperative loop: integrations get a slice, then the UI, then LVGL. No
// integration is allowed to block for long, which is why the BLE client and the
// BMS parameter reads were reworked during the port. If something ever has to
// block, give it its own FreeRTOS task and let it publish through entities.
// -----------------------------------------------------------------------------
#include <Arduino.h>

#include "board_indicator_d1.h"
#include "bsp/display.h"
#include "core/alarms.h"
#include "core/integration.h"
#include "core/settings.h"
#include "integrations/bms_jbd.h"
#include "integrations/bthome_sensor.h"
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
  cc::Settings::instance().begin();

  if (!bsp::displayBegin()) {
    log_e("display failed to start - continuing headless");
  }

  cc::Hub& hub = cc::Hub::instance();
  hub.add(&cc::bms());
  hub.add(&cc::btHomeSensors());
  // Future accessories land here: relay nodes, tank senders, the heater.
  hub.begin();

  cc::ui::begin();
  log_i("boot complete, free heap %u", (unsigned)ESP.getFreeHeap());
}

void loop() {
  cc::Settings::instance().loop();  // commits anything an integration handed over
  cc::Hub::instance().loop();
  serviceButton();
  serviceAlarmOutput();
  cc::ui::tick();
  bsp::displayLoop();
}
