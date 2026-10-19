#include "core/clock.h"

namespace cc {
namespace {

// Days from 1970-01-01 to y-m-d, by Howard Hinnant's civil calendar algorithm.
// Chosen over a table of month lengths because the leap-year handling is the
// part people get wrong, and here it falls out of the era arithmetic instead of
// being a special case anyone has to remember.
int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);              // [0, 399]
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;  // [0, 146096]
  return era * 146097 + (int32_t)doe - 719468;
}

void civilFromDays(int32_t z, int32_t& y, uint32_t& m, uint32_t& d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);           // [0, 146096]
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int32_t)yoe + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y += (m <= 2);
}

}  // namespace

Clock& Clock::instance() {
  static Clock c;
  return c;
}

void Clock::tick(uint32_t nowMs) {
  if (!set_) return;
  // Unsigned, so this stays right across the millis() wrap.
  const uint32_t elapsed = nowMs - lastMs_;
  if (elapsed < 1000) return;
  const uint32_t secs = elapsed / 1000;
  epoch_ += secs;
  // Leave the remainder behind rather than resetting to nowMs, or the clock
  // loses up to a second on every tick and runs slow by minutes a day.
  lastMs_ += secs * 1000;
}

void Clock::set(Epoch e, uint32_t nowMs, bool approximate) {
  epoch_ = e;
  lastMs_ = nowMs;
  set_ = true;
  approximate_ = approximate;
}

namespace {

// Steps v by delta inside [lo, hi], wrapping at both ends. Done in int32 so a
// uint8 field stepped down from its minimum does not wrap to 255 first.
int32_t stepWrapped(int32_t v, int32_t delta, int32_t lo, int32_t hi) {
  const int32_t span = hi - lo + 1;
  int32_t out = (v - lo + delta) % span;
  if (out < 0) out += span;
  return out + lo;
}

}  // namespace

void stepDateField(DateTime& dt, DateField f, int delta) {
  switch (f) {
    case DateField::Year:
      dt.year = (uint16_t)stepWrapped(dt.year, delta, kMinYear, kMaxYear);
      break;
    case DateField::Month:
      dt.month = (uint8_t)stepWrapped(dt.month, delta, 1, 12);
      break;
    case DateField::Day:
      dt.day = (uint8_t)stepWrapped(dt.day, delta, 1,
                                    Clock::daysInMonth(dt.year, dt.month));
      break;
    case DateField::Hour:
      dt.hour = (uint8_t)stepWrapped(dt.hour, delta, 0, 23);
      break;
    case DateField::Minute:
      dt.minute = (uint8_t)stepWrapped(dt.minute, delta, 0, 59);
      break;
    default:
      return;
  }

  // Changing the year or the month can strand the day past the end of it -
  // 31 March stepped back to February. Clamp rather than roll over.
  const uint8_t last = Clock::daysInMonth(dt.year, dt.month);
  if (dt.day > last) dt.day = last;
  dt.second = 0;  // a hand-set clock is accurate to the minute at best
}

const char* dateFieldName(DateField f) {
  switch (f) {
    case DateField::Year:   return "Year";
    case DateField::Month:  return "Month";
    case DateField::Day:    return "Day";
    case DateField::Hour:   return "Hour";
    case DateField::Minute: return "Min";
    default:                return "?";
  }
}

bool Clock::isLeapYear(uint16_t y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

uint8_t Clock::daysInMonth(uint16_t y, uint8_t m) {
  static const uint8_t kDays[13] = {0, 31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 30;
  if (m == 2 && isLeapYear(y)) return 29;
  return kDays[m];
}

DateTime Clock::breakDown(Epoch e) {
  DateTime dt;
  const uint32_t days = e / kSecondsPerDay;
  uint32_t rem = e % kSecondsPerDay;

  dt.hour = (uint8_t)(rem / 3600);
  rem %= 3600;
  dt.minute = (uint8_t)(rem / 60);
  dt.second = (uint8_t)(rem % 60);

  int32_t y = 1970;
  uint32_t m = 1, d = 1;
  civilFromDays((int32_t)days, y, m, d);
  dt.year = (uint16_t)y;
  dt.month = (uint8_t)m;
  dt.day = (uint8_t)d;
  return dt;
}

Epoch Clock::make(const DateTime& dt) {
  const int32_t days = daysFromCivil(dt.year, dt.month, dt.day);
  if (days < 0) return 0;  // before 1970; nothing here can mean that
  return (Epoch)days * kSecondsPerDay + dt.hour * 3600u + dt.minute * 60u +
         dt.second;
}

const char* Clock::formatDate(Epoch e, char* buf, size_t len) {
  if (!buf || len == 0) return "";
  const DateTime dt = breakDown(e);
  snprintf(buf, len, "%04u-%02u-%02u", (unsigned)dt.year, (unsigned)dt.month,
           (unsigned)dt.day);
  return buf;
}

const char* Clock::formatTime(Epoch e, char* buf, size_t len) {
  if (!buf || len == 0) return "";
  const DateTime dt = breakDown(e);
  snprintf(buf, len, "%02u:%02u", (unsigned)dt.hour, (unsigned)dt.minute);
  return buf;
}

const char* Clock::formatStamp(Epoch e, char* buf, size_t len) {
  if (!buf || len == 0) return "";
  const DateTime dt = breakDown(e);
  snprintf(buf, len, "%04u-%02u-%02u %02u:%02u", (unsigned)dt.year,
           (unsigned)dt.month, (unsigned)dt.day, (unsigned)dt.hour,
           (unsigned)dt.minute);
  return buf;
}

const char* Clock::formatDuration(uint64_t ms, char* buf, size_t len) {
  if (!buf || len == 0) return "";
  const uint32_t secs = (uint32_t)(ms / 1000ULL);
  const uint32_t days = secs / kSecondsPerDay;
  const uint32_t rem = secs % kSecondsPerDay;
  // Days are only printed once there are some: "0d 02:17:33" reads like a
  // field that will never be used, and on a van controller it is the common
  // case for the first day after every power cut.
  if (days > 0) {
    snprintf(buf, len, "%ud %02u:%02u:%02u", (unsigned)days,
             (unsigned)(rem / 3600), (unsigned)((rem % 3600) / 60),
             (unsigned)(rem % 60));
  } else {
    snprintf(buf, len, "%02u:%02u:%02u", (unsigned)(rem / 3600),
             (unsigned)((rem % 3600) / 60), (unsigned)(rem % 60));
  }
  return buf;
}

}  // namespace cc
