#pragma once
#include <Arduino.h>

#include "config.h"

// -----------------------------------------------------------------------------
// A ring of recent readings for one sensor, so the Climate page can show where
// a room is heading rather than only where it is.
//
// Fixed size, no allocation - the same idiom as the alarm table. Samples are
// two int16s rather than two floats because the board's tight resource is
// internal RAM (docs/ARCHITECTURE.md), and a graph a few hundred pixels wide
// cannot show more than a tenth of a degree anyway.
//
// Nothing here touches hardware, so it is covered by `pio test -e native`.
// -----------------------------------------------------------------------------

namespace cc {

class History {
 public:
  static constexpr size_t kSamples = CFG_HISTORY_SAMPLES;
  static constexpr uint32_t kIntervalMs = CFG_HISTORY_INTERVAL_MS;

  // A gap: the sensor was heard but had nothing to say about this quantity.
  // INT16_MIN rather than 0, because 0 is a temperature a van actually reaches.
  static constexpr int16_t kNoValue = INT16_MIN;

  void clear();

  // Appends a sample if kIntervalMs has elapsed since the last one, and returns
  // true when it did. Cheap enough to call from loop() at any rate; the
  // interval gate lives here so no caller has to keep its own timer.
  //
  // The first call always records, so a graph has something in it long before
  // the first interval is up.
  bool record(uint32_t nowMs, bool haveTemp, float tempC, bool haveHum,
              float humPct);

  size_t count() const { return count_; }
  bool empty() const { return count_ == 0; }

  // Oldest first: i in [0, count()). Out of range reads as kNoValue.
  int16_t tempAt(size_t i) const;  // tenths of a degree Celsius
  int16_t humAt(size_t i) const;   // whole percent

  // How far back the oldest sample sits, in milliseconds.
  uint32_t spanMs() const;

  // Extent of the samples that carry a value. False if none do, in which case
  // lo and hi are untouched.
  bool tempRangeC(float& lo, float& hi) const;
  bool humRange(float& lo, float& hi) const;

 private:
  int16_t at(const int16_t* ring, size_t i) const;

  int16_t temp_[kSamples] = {0};
  int16_t hum_[kSamples] = {0};
  size_t count_ = 0;
  size_t head_ = 0;  // next slot to write; count_ == kSamples means wrapped
  uint32_t lastMs_ = 0;
  bool started_ = false;
};

}  // namespace cc
