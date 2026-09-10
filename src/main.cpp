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

// -----------------------------------------------------------------------------
// SPIKE 2026-09-10: does the Wi-Fi radio coexist with BLE on this board?
//
// The lighting plan wants ESP-NOW, which runs on the Wi-Fi radio, while the van
// needs BLE continuously - a connection to the BMS and a permanent scan for
// sensors. The ESP32-S3 has one 2.4 GHz radio and time-slices all of it. The
// question is what that costs, measured against the baselines already recorded
// in ROADMAP.md: ~4.93 advertisements/sec, ~46,500 loops per 2 s heartbeat,
// ~95 KB free heap.
//
//   0 = off, the measured baseline
//   1 = Wi-Fi STA + esp_now_init(), radio up but silent
//   2 = as 1, plus an ESP-NOW broadcast every 250 ms, standing in for light
//       commands
//
// Throwaway. Findings go to the roadmap; this does not merge as-is.
// -----------------------------------------------------------------------------
#define SPIKE_WIFI 0

#if SPIKE_WIFI
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#endif

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

#if SPIKE_WIFI
namespace {

uint8_t g_bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint32_t g_espnowSent = 0;
uint32_t g_espnowFailed = 0;
uint32_t g_lastSendMs = 0;
uint32_t g_lastWifiReportMs = 0;

void spikeWifiBegin() {
  const uint32_t before = ESP.getFreeHeap();
  // Station mode without joining anything: ESP-NOW needs the Wi-Fi driver
  // started but not a network. This is what the lighting plan would do.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  const uint32_t afterWifi = ESP.getFreeHeap();

  const bool ok = esp_now_init() == ESP_OK;
  const uint32_t afterNow = ESP.getFreeHeap();

  if (ok) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, g_bcast, 6);
    peer.channel = 0;
    peer.encrypt = false;  // a real node would use PMK/LMK; this is throughput
    esp_now_add_peer(&peer);
  }

  log_i("SPIKE wifi: init=%s heap %u -> %u (wifi) -> %u (espnow), cost %u B",
        ok ? "ok" : "FAILED", (unsigned)before, (unsigned)afterWifi,
        (unsigned)afterNow, (unsigned)(before - afterNow));
}

void spikeWifiLoop() {
#if SPIKE_WIFI >= 2
  if (millis() - g_lastSendMs >= 250) {
    g_lastSendMs = millis();
    uint8_t payload[16] = {0};
    payload[0] = 0xC0;
    if (esp_now_send(g_bcast, payload, sizeof(payload)) == ESP_OK) {
      g_espnowSent++;
    } else {
      g_espnowFailed++;
    }
  }
#endif
  if (millis() - g_lastWifiReportMs >= 10000) {
    g_lastWifiReportMs = millis();
    log_i("SPIKE wifi: sent=%u failed=%u heap=%u ch=%u",
          (unsigned)g_espnowSent, (unsigned)g_espnowFailed,
          (unsigned)ESP.getFreeHeap(), (unsigned)WiFi.channel());
  }
}

}  // namespace
#endif

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

#if SPIKE_WIFI
  spikeWifiBegin();  // after BLE, which is the incremental question
#endif
  log_i("boot complete, free heap %u", (unsigned)ESP.getFreeHeap());
}

void loop() {
#if SPIKE_WIFI
  spikeWifiLoop();
#endif
  cc::Settings::instance().loop();  // commits anything an integration handed over
  cc::Hub::instance().loop();
  serviceButton();
  serviceAlarmOutput();
  cc::ui::tick();
  bsp::displayLoop();
}
