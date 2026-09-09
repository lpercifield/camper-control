#include "core/integration.h"

#include <esp_timer.h>

namespace cc {

const char* linkStateName(LinkState s) {
  switch (s) {
    case LinkState::Offline:    return "offline";
    case LinkState::Searching:  return "searching";
    case LinkState::Connecting: return "connecting";
    case LinkState::Online:     return "online";
    case LinkState::Fault:      return "fault";
    default:                    return "?";
  }
}

Hub& Hub::instance() {
  static Hub h;
  return h;
}

void Hub::add(Integration* i) {
  if (i) integrations_.push_back(i);
}

void Hub::begin() {
  for (Integration* i : integrations_) {
    uint32_t t0 = millis();
    bool ok = i->begin();
    log_i("integration '%s' begin: %s (%lu ms)", i->name(), ok ? "ok" : "FAILED",
          (unsigned long)(millis() - t0));
  }
}

void Hub::loop() {
  // esp_timer_get_time() rather than millis(), and one reading per boundary
  // rather than two per integration. millis() is esp_timer_get_time()/1000 - a
  // 64-bit division - and this runs tens of thousands of times a second, so the
  // instrumentation was costing more than the integrations it measures.
  // Adding a second integration whose loop() does almost nothing cost ~12% of
  // the main loop rate (46,700 to 40,800 per 2 s heartbeat) until this changed.
  int64_t t = esp_timer_get_time();
  for (Integration* i : integrations_) {
    i->loop();
    const int64_t now = esp_timer_get_time();
    const int64_t elapsedUs = now - t;
    t = now;
    // ARCHITECTURE.md rule 1 says loop() returns promptly, and nothing enforced
    // it. A stalled integration presents as missed touches and a frozen UI,
    // which is a miserable thing to diagnose from the symptom, so name it here.
    if (elapsedUs > static_cast<int64_t>(kSlowLoopWarnMs) * 1000) {
      log_w("integration '%s' held the loop for %lu ms", i->name(),
            (unsigned long)(elapsedUs / 1000));
    }
  }
}

}  // namespace cc
