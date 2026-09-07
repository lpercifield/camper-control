#pragma once
#include <Arduino.h>

namespace cc {

enum class Severity : uint8_t { Info = 0, Warning = 1, Critical = 2 };

struct Alarm {
  const char* id = nullptr;   // stable key, e.g. "bms.cell_over"
  const char* text = nullptr; // what the crew reads at 3am
  Severity severity = Severity::Info;
  bool active = false;
  uint32_t sinceMs = 0;
};

// A small fixed-size alarm table. Integrations raise and clear by id; the UI
// shows the worst active one and the crew can silence the audible part without
// clearing the condition.
class Alarms {
 public:
  static constexpr size_t kMax = 16;

  static Alarms& instance();

  void raise(const char* id, const char* text, Severity sev);
  void clear(const char* id);
  void clearPrefix(const char* prefix);  // e.g. drop every "bms." alarm

  bool any() const;
  Severity worst() const;
  const Alarm* worstAlarm() const;
  size_t activeCount() const;

  // Audible silencing. Auto-unmutes once everything clears, so the next fault
  // still gets your attention.
  void silence() { silenced_ = true; }
  bool silenced() const { return silenced_; }
  bool shouldSound() const;

  size_t size() const { return count_; }
  const Alarm& at(size_t i) const { return table_[i]; }

 private:
  Alarms() = default;
  Alarm* findOrCreate(const char* id);

  Alarm table_[kMax];
  size_t count_ = 0;
  bool silenced_ = false;
};

}  // namespace cc
