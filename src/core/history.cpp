#include "core/history.h"

namespace cc {
namespace {

// Clamp before the cast: a decoder bug that produces 4000 C must not wrap into
// a plausible-looking negative. See docs/decisions/0011 - a wrong reading that
// reads as a reading is the worst failure mode available.
int16_t quantise(float v, float scale) {
  const float s = v * scale;
  if (s > 32000.0f) return 32000;
  if (s < -32000.0f) return -32000;
  return (int16_t)(s + (s >= 0.0f ? 0.5f : -0.5f));
}

}  // namespace

void History::clear() {
  count_ = 0;
  head_ = 0;
  lastMs_ = 0;
  started_ = false;
}

bool History::record(uint32_t nowMs, bool haveTemp, float tempC, bool haveHum,
                     float humPct) {
  // Unsigned subtraction, so this stays correct across the 49-day millis()
  // wrap that this device is expected to see - it lives in a van, not on a
  // bench.
  if (started_ && (nowMs - lastMs_) < kIntervalMs) return false;

  temp_[head_] = haveTemp ? quantise(tempC, 10.0f) : kNoValue;
  hum_[head_] = haveHum ? quantise(humPct, 1.0f) : kNoValue;

  head_ = (head_ + 1) % kSamples;
  if (count_ < kSamples) count_++;
  lastMs_ = nowMs;
  started_ = true;
  return true;
}

int16_t History::at(const int16_t* ring, size_t i) const {
  if (i >= count_) return kNoValue;
  // head_ is the next slot to write, so the oldest sample is the one it is
  // about to overwrite once the ring has wrapped.
  const size_t oldest = (count_ == kSamples) ? head_ : 0;
  return ring[(oldest + i) % kSamples];
}

int16_t History::tempAt(size_t i) const { return at(temp_, i); }
int16_t History::humAt(size_t i) const { return at(hum_, i); }

uint32_t History::spanMs() const {
  if (count_ < 2) return 0;
  return (uint32_t)(count_ - 1) * kIntervalMs;
}

bool History::tempRangeC(float& lo, float& hi) const {
  bool any = false;
  int16_t mn = 0, mx = 0;
  for (size_t i = 0; i < count_; i++) {
    const int16_t v = tempAt(i);
    if (v == kNoValue) continue;
    if (!any || v < mn) mn = v;
    if (!any || v > mx) mx = v;
    any = true;
  }
  if (!any) return false;
  lo = mn / 10.0f;
  hi = mx / 10.0f;
  return true;
}

bool History::humRange(float& lo, float& hi) const {
  bool any = false;
  int16_t mn = 0, mx = 0;
  for (size_t i = 0; i < count_; i++) {
    const int16_t v = humAt(i);
    if (v == kNoValue) continue;
    if (!any || v < mn) mn = v;
    if (!any || v > mx) mx = v;
    any = true;
  }
  if (!any) return false;
  lo = (float)mn;
  hi = (float)mx;
  return true;
}

}  // namespace cc
