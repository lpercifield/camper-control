#include "integrations/system_info.h"

#include <lvgl.h>

#include "bsp/display.h"
#include "config.h"
#include "core/clock.h"
#include "core/registry.h"
#if CFG_BLE_BROADCAST
#include "integrations/ble_broadcast.h"
#endif

namespace cc {
namespace {

// Entity ids are literals by contract - `Entity` holds the pointer, it does
// not copy - so the link rows draw their ids from here rather than building
// them. This table's length is what caps SystemInfo::kMaxLinks.
const char* const kLinkIds[] = {"system.link0", "system.link1", "system.link2",
                                "system.link3", "system.link4", "system.link5"};

}  // namespace

SystemInfo& systemInfo() {
  static SystemInfo instance;
  return instance;
}

bool SystemInfo::begin() {
  static_assert(sizeof(kLinkIds) / sizeof(kLinkIds[0]) == SystemInfo::kMaxLinks,
                "kLinkIds and kMaxLinks have to agree");

  Registry& reg = Registry::instance();
  reg.add(&uptime_);
  reg.add(&heap_);
  reg.add(&heapMin_);
  reg.add(&pool_);
  reg.add(&loopRate_);
  reg.add(&flushRate_);
#if CFG_BLE_BROADCAST
  reg.add(&bleRate_);
#endif
  reg.add(&i2c_);

  // Hub order is known by now only because this integration is added last in
  // setup(). Every other integration has had begin() called and has set its
  // own link state, so the first sample says something true.
  Hub& hub = Hub::instance();
  size_t wanted = 0;
  for (size_t i = 0; i < hub.size(); i++) {
    // No row for ourselves: "System: running" is a tautology taking up a row
    // on the page it is reporting from.
    if (hub.at(i) != this) wanted++;
  }
  if (wanted > kMaxLinks) {
    log_w("system: %u integrations, only %u get a link row", (unsigned)wanted,
          (unsigned)kMaxLinks);
    wanted = kMaxLinks;
  }
  links_.reserve(wanted);
  for (size_t i = 0; i < hub.size() && links_.size() < wanted; i++) {
    Integration* in = hub.at(i);
    if (in == this) continue;
    // The name is the integration's own, which is a literal with static
    // lifetime - the one case where borrowing a pointer for a label is safe.
    links_.emplace_back(kLinkIds[links_.size()], in->name(), Domain::System);
    reg.add(&links_.back());
  }

  // Once, at begin(): 112 bus transactions is not something to repeat at 4 Hz,
  // and what is soldered to the bus does not change while the van is running.
  char bus[TextEntity::kMaxLen];
  bsp::scanI2C("system screen", bus, sizeof(bus));
  i2c_.set(bus);

  lastSampleMs_ = millis();
  lastFrameSampleMs_ = lastSampleMs_;
  lastLoops_ = bsp::loopCount();
  lastFlushes_ = bsp::flushCount();
  // Online rather than Offline, and that is load-bearing: the header shows the
  // first integration that is not Online, so an honest "not applicable" here
  // would hide a real fault somewhere else.
  setLink(LinkState::Online, "running");
  return true;
}

void SystemInfo::loop() {
  // Gated on a counter rather than on the clock, the same way main.cpp gates
  // Clock::tick() and for the same reason: millis() is a 64-bit division and
  // this runs ~19,000 times a second, so asking it every pass is the whole
  // cost of this integration. Measured on hardware 2026-10-18 - 38,289 loops
  // per heartbeat with the call every pass, 39,750 with this gate, against a
  // 41,000 baseline. Every 256th pass is still ~75 Hz against a 1 Hz sample.
  if ((++skip_ & 0xFF) != 0) return;
  const uint32_t now = millis();
  if (now - lastSampleMs_ < kSampleMs) return;
  sample(now);
}

void SystemInfo::sample(uint32_t nowMs) {
  const uint32_t elapsed = nowMs - lastSampleMs_;  // wrap-safe
  lastSampleMs_ = nowMs;
  uptimeMs_ += elapsed;

  char buf[TextEntity::kMaxLen];
  uptime_.set(Clock::formatDuration(uptimeMs_, buf, sizeof(buf)));

  heap_.set(ESP.getFreeHeap() / 1024.0f);
  heapMin_.set(ESP.getMinFreeHeap() / 1024.0f);

  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  pool_.set((float)mon.used_pct);

  // Rates from the deltas, divided by the window actually measured rather than
  // by kSampleMs: the loop is cooperative, so a slow integration stretches the
  // window and dividing by the nominal figure would report a dip that is
  // really the measurement being late.
  const uint32_t loops = bsp::loopCount();
  const uint32_t flushes = bsp::flushCount();
  loopRate_.set((loops - lastLoops_) * 1000.0f / elapsed);
  flushRate_.set((flushes - lastFlushes_) * 1000.0f / elapsed);
  lastLoops_ = loops;
  lastFlushes_ = flushes;

#if CFG_BLE_BROADCAST
  // Its own window - see kFrameWindowMs. Until the first one closes the row
  // reads "--", which is honest: we do not know the rate yet.
  const uint32_t frameWindow = nowMs - lastFrameSampleMs_;
  if (frameWindow >= kFrameWindowMs) {
    const uint32_t frames = bleBroadcast().frames();
    bleRate_.set((frames - lastFrames_) * 60000.0f / frameWindow);
    lastFrames_ = frames;
    lastFrameSampleMs_ = nowMs;
  }
#endif

  Hub& hub = Hub::instance();
  size_t row = 0;
  for (size_t i = 0; i < hub.size() && row < links_.size(); i++) {
    Integration* in = hub.at(i);
    if (in == this) continue;
    const char* text = in->statusText();
    // The same rule the header follows, so the two never disagree: an
    // integration's own words when it has any, the link state otherwise.
    links_[row].set(text && text[0] ? text : linkStateName(in->link()));
    row++;
  }
}

}  // namespace cc
