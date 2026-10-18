#pragma once
#include <Arduino.h>

#include "core/clock.h"
#include "core/history.h"

// -----------------------------------------------------------------------------
// The shape of a day's sensor log, and the daily high and low computed from it.
//
// **Not the SD card.** The microSD slot on this board belongs to the RP2040 and
// the ESP32-S3 has no path to it - probed 2026-09-17, see docs/HARDWARE.md.
// These files live on the internal flash, in the 1.5 MB `spiffs` partition the
// build already reserves and nothing else uses. `bsp/storage.*` does the actual
// filesystem work; everything here is pure text and arithmetic so it can be
// tested on a laptop, because a log that silently writes the wrong number is
// worth less than no log at all.
//
// One file per local day, `/log/YYYY-MM-DD.csv`:
//
//     # camper-control log 1
//     # date 2026-09-17
//     # sensor 460a4a f3:a9:c3:46:0a:4a Fridge
//     843,460a4a,214,38,100
//     843,460c33,195,42,87
//
// A row is minute-of-day, sensor key, tenths of a degree Celsius, whole percent
// humidity, whole percent battery. Tenths and whole percent because that is all
// the sensors report, and `History::kNoValue` marks anything the sensor did not
// send - a log that repeated the last reading through an outage would be a
// plausible lie, which is the failure this project guards hardest against.
//
// **The key is the last six hex digits of the MAC, not the slot index.** Slots
// are positional and a sensor takes whichever is free in discovery order, so
// after a reboot - or after the table reuses a quiet slot - slot 0 is very
// likely a different sensor. Replaying yesterday's numbers onto the wrong room
// is exactly the kind of quietly-wrong that nobody notices, so the row carries
// something that belongs to the sensor itself.
//
// Temperatures are stored in **Celsius**, like everywhere else in this project,
// regardless of what the screen is set to. See core/units.h.
// -----------------------------------------------------------------------------

namespace cc {

// Big enough for any row or header line this writes, including the newline.
constexpr size_t kLogLineMax = 96;

constexpr const char* kLogDir = "/log";
constexpr int kLogFormatVersion = 1;

// Length of a sensor key, plus the NUL: six lowercase hex digits.
constexpr size_t kSensorKeyLen = 7;

struct LogRecord {
  uint16_t minuteOfDay = 0;  // 0..1439
  char key[kSensorKeyLen] = {0};
  int16_t tempCx10 = History::kNoValue;
  int16_t humPct = History::kNoValue;
  int16_t battPct = History::kNoValue;
};

// "f3:a9:c3:46:0a:4a" -> "460a4a". Lowercase, always six digits. False if the
// address is not one we can key on, in which case nothing should be logged for
// it rather than everything being logged under an empty key.
bool sensorKey(const char* mac, char* out, size_t len);

// "/log/2026-09-17.csv". Returns buf, never null.
const char* dayFilePath(Epoch e, char* buf, size_t len);

// True if `name` (a bare filename, no directory) is one of our day files, and
// if so its day index. Used by rotation, which must not delete anything it did
// not write.
bool parseDayFileName(const char* name, uint32_t& dayIndexOut);

// Rotation: keep the newest `keepDays` days, counting today. Anything older is
// fair game. Returns true when `name` should be deleted.
bool shouldRotate(const char* name, uint32_t today, uint32_t keepDays);

// Formats a row, newline included. Returns the length written, 0 on failure.
size_t formatRow(const LogRecord& r, char* buf, size_t len);
bool parseRow(const char* line, LogRecord& out);

// The `# sensor <key> <mac> <name>` line. A sensor gets one when it first
// appears in a day, so the file stands on its own without the firmware that
// wrote it - the keys in the rows mean nothing on their own.
size_t formatSensorHeader(const char* key, const char* mac, const char* name,
                          char* buf, size_t len);

// ---- the daily high and low -------------------------------------------------
// Fed from the same per-minute stream as the log rather than from the graph's
// ring: the ring samples every ten minutes and would walk straight past the
// hour the sun was on the roof.
struct DayExtremes {
  uint32_t day = 0;
  bool haveTemp = false;
  bool haveHum = false;
  float tMinC = 0.0f;
  float tMaxC = 0.0f;
  float hMin = 0.0f;
  float hMax = 0.0f;

  void reset(uint32_t dayIndex);
  void observe(bool haveT, float tempC, bool haveH, float humPct);
  // Folds a logged row back in, so a reboot does not lose the morning.
  void observeRecord(const LogRecord& r);
};

}  // namespace cc
