#pragma once
#include <Arduino.h>
#include <vector>

namespace cc {

enum class LinkState : uint8_t {
  Offline,     // not started, or given up
  Searching,   // scanning / looking for the device
  Connecting,
  Online,
  Fault        // connected but the device is reporting a problem
};

const char* linkStateName(LinkState s);

// Everything that talks to hardware is an Integration: the BMS over BLE today,
// a relay node over ESP-NOW or a tank sender on a Grove port tomorrow. The rules
// are simple - begin() must not block forever, loop() must return promptly, and
// all data reaches the rest of the system through registered entities.
class Integration {
 public:
  virtual ~Integration() = default;

  virtual const char* name() const = 0;
  virtual bool begin() = 0;   // register entities, start drivers
  virtual void loop() = 0;    // called every pass of the main loop

  LinkState link() const { return link_; }
  const char* statusText() const { return statusText_; }

 protected:
  void setLink(LinkState s, const char* text = nullptr) {
    if (s != link_) {
      log_i("%s: %s -> %s", name(), linkStateName(link_), linkStateName(s));
    }
    link_ = s;
    if (text) statusText_ = text;
  }

 private:
  LinkState link_ = LinkState::Offline;
  const char* statusText_ = "";
};

// Owns the integration list and drives it. Kept deliberately small: cooperative
// scheduling on the Arduino loop, no tasks. If an integration ever needs to
// block, it gets its own FreeRTOS task and posts results back through entities.
class Hub {
 public:
  // Anything above this is long enough for a person to notice the UI stop
  // responding, so it gets logged with the name of whoever caused it.
  static constexpr uint32_t kSlowLoopWarnMs = 50;

  static Hub& instance();

  void add(Integration* i);
  void begin();
  void loop();

  size_t size() const { return integrations_.size(); }
  Integration* at(size_t i) const { return integrations_[i]; }

 private:
  Hub() = default;
  std::vector<Integration*> integrations_;
};

}  // namespace cc
