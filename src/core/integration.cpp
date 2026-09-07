#include "core/integration.h"

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
  for (Integration* i : integrations_) {
    const uint32_t t0 = millis();
    i->loop();
    const uint32_t elapsed = millis() - t0;
    // ARCHITECTURE.md rule 1 says loop() returns promptly, and nothing enforced
    // it. A stalled integration presents as missed touches and a frozen UI,
    // which is a miserable thing to diagnose from the symptom, so name it here.
    if (elapsed > kSlowLoopWarnMs) {
      log_w("integration '%s' held the loop for %lu ms", i->name(),
            (unsigned long)elapsed);
    }
  }
}

}  // namespace cc
