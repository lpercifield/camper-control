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
    i->loop();
  }
}

}  // namespace cc
