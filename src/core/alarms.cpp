#include "core/alarms.h"
#include <string.h>

namespace cc {

Alarms& Alarms::instance() {
  static Alarms a;
  return a;
}

Alarm* Alarms::findOrCreate(const char* id) {
  for (size_t i = 0; i < count_; i++) {
    if (strcmp(table_[i].id, id) == 0) return &table_[i];
  }
  if (count_ >= kMax) {
    log_e("alarm table full, dropping '%s'", id);
    return nullptr;
  }
  // Alarms are now raised from the BMS task while the UI reads them from the
  // main loop. Fill the slot completely before publishing it, or a reader can
  // see a slot that count_ says exists but whose id is still null.
  Alarm* a = &table_[count_];
  a->id = id;
  a->text = nullptr;
  a->severity = Severity::Info;
  a->active = false;
  a->sinceMs = 0;
  __sync_synchronize();
  count_++;
  return a;
}

void Alarms::raise(const char* id, const char* text, Severity sev) {
  Alarm* a = findOrCreate(id);
  if (!a) return;
  if (!a->active) {
    a->sinceMs = millis();
    log_w("ALARM %s: %s", id, text);
  }
  a->text = text;
  a->severity = sev;
  a->active = true;
}

void Alarms::clear(const char* id) {
  for (size_t i = 0; i < count_; i++) {
    if (strcmp(table_[i].id, id) == 0) {
      if (table_[i].active) log_i("alarm cleared: %s", id);
      table_[i].active = false;
      break;
    }
  }
  if (!any()) silenced_ = false;
}

void Alarms::clearPrefix(const char* prefix) {
  size_t n = strlen(prefix);
  for (size_t i = 0; i < count_; i++) {
    if (strncmp(table_[i].id, prefix, n) == 0) table_[i].active = false;
  }
  if (!any()) silenced_ = false;
}

bool Alarms::any() const {
  for (size_t i = 0; i < count_; i++) {
    if (table_[i].active) return true;
  }
  return false;
}

size_t Alarms::activeCount() const {
  size_t n = 0;
  for (size_t i = 0; i < count_; i++) {
    if (table_[i].active) n++;
  }
  return n;
}

const Alarm* Alarms::worstAlarm() const {
  const Alarm* best = nullptr;
  for (size_t i = 0; i < count_; i++) {
    if (!table_[i].active) continue;
    if (!best || table_[i].severity > best->severity) best = &table_[i];
  }
  return best;
}

Severity Alarms::worst() const {
  const Alarm* a = worstAlarm();
  return a ? a->severity : Severity::Info;
}

bool Alarms::shouldSound() const {
  if (silenced_) return false;
  const Alarm* a = worstAlarm();
  return a && a->severity >= Severity::Warning;
}

}  // namespace cc
