#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------------
// A wall clock for a device that has no way of knowing what time it is.
//
// There is no RTC on this board, no Wi-Fi and no NTP (docs/HARDWARE.md), so the
// only source of truth is a person typing it in. That makes three things true,
// and the logging and graphing above this both depend on all three:
//
//   - The clock counts from millis(), so it drifts with the 40 MHz crystal -
//     seconds a day, which is nothing against a daily bucket.
//   - It is **local time with no timezone**. The van is in one place at a time
//     and the crew sets it to whatever the wall says. `dayIndex()` is therefore
//     a local day, and midnight is a real boundary rather than a UTC one.
//   - After a power cut it is wrong by however long the power was off.
//     `Settings` restores the last value it wrote and the clock reports itself
//     `approximate()` until a person confirms it. A wrong timestamp that says
//     it is a guess is useful; one that does not is the worst kind.
//
// Pure arithmetic, so it runs on a laptop under `pio test -e native`.
// -----------------------------------------------------------------------------

namespace cc {

// Seconds since 1970-01-01 00:00:00, local. Unsigned, so this is good until
// 2106 - long after the van is scrap.
using Epoch = uint32_t;

struct DateTime {
  uint16_t year = 1970;
  uint8_t month = 1;   // 1-12
  uint8_t day = 1;     // 1-31
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
};

constexpr uint32_t kSecondsPerDay = 86400UL;

// The clock can only be set by hand, so the fields a person steps up and down
// are part of the contract rather than a detail of the screen that draws them.
enum class DateField : uint8_t { Year, Month, Day, Hour, Minute, COUNT };

// The year range the setter offers. Not arbitrary: below this is a clock
// nobody meant to set, and above it is a van that has outlived its firmware.
constexpr uint16_t kMinYear = 2024;
constexpr uint16_t kMaxYear = 2099;

// Steps one field by delta, wrapping within that field's range, and leaves the
// result a real date. Stepping the month off the end of a long month shortens
// the day rather than rolling into the next month - 31 January stepped to
// February is 28 February, not 3 March, because a setter that moves a field
// you did not touch is a setter you cannot trust.
void stepDateField(DateTime& dt, DateField f, int delta);

// The label under each stepper column.
const char* dateFieldName(DateField f);

class Clock {
 public:
  static Clock& instance();

  bool isSet() const { return set_; }
  // True when the time was restored from NVS rather than entered by a person,
  // so it is stale by however long the board was off.
  bool approximate() const { return approximate_; }

  Epoch now() const { return epoch_; }

  // Moves the clock forward. Call it from the main loop as often as you like.
  //
  // Wrap-safe and lossless: it moves *whole seconds* out of the millisecond
  // counter and leaves the remainder behind, so neither the 49-day millis()
  // wrap nor a caller that ticks at an awkward rate can make it drift. A
  // device that lives in a van will see that wrap.
  void tick(uint32_t nowMs);

  // Sets the time. `approximate` marks it as restored rather than known.
  void set(Epoch e, uint32_t nowMs, bool approximate = false);

  // Local days since the epoch. Changes at local midnight, which is what makes
  // a "daily" file and a daily high and low mean what they say.
  uint32_t dayIndex() const { return epoch_ / kSecondsPerDay; }
  static uint32_t dayIndexOf(Epoch e) { return e / kSecondsPerDay; }
  uint32_t secondOfDay() const { return epoch_ % kSecondsPerDay; }
  uint32_t minuteOfDay() const { return (epoch_ % kSecondsPerDay) / 60; }

  // ---- calendar ----
  static DateTime breakDown(Epoch e);
  static Epoch make(const DateTime& dt);
  static bool isLeapYear(uint16_t y);
  static uint8_t daysInMonth(uint16_t y, uint8_t m);

  // "2026-09-17", exactly 10 characters plus NUL. Returns buf.
  static const char* formatDate(Epoch e, char* buf, size_t len);
  // "14:03", 5 characters plus NUL.
  static const char* formatTime(Epoch e, char* buf, size_t len);
  // "2026-09-17 14:03"
  static const char* formatStamp(Epoch e, char* buf, size_t len);

#ifdef CC_NATIVE_TEST
  void testReset() {
    epoch_ = 0;
    lastMs_ = 0;
    set_ = false;
    approximate_ = false;
  }
#endif

 private:
  Clock() = default;

  Epoch epoch_ = 0;
  uint32_t lastMs_ = 0;
  bool set_ = false;
  bool approximate_ = false;
};

}  // namespace cc
