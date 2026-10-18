#include "core/datalog.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace cc {
namespace {

bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

// Reads one comma-separated integer and advances p past the separator.
// Returns false on anything that is not a number, so a truncated or corrupt
// line is rejected whole rather than half-read into a plausible reading.
bool nextInt(const char*& p, long& out) {
  if (p == nullptr || *p == '\0') return false;
  char* end = nullptr;
  const long v = strtol(p, &end, 10);
  if (end == p) return false;
  out = v;
  p = end;
  if (*p == ',') p++;
  return true;
}

}  // namespace

bool sensorKey(const char* mac, char* out, size_t len) {
  if (!mac || !out || len < kSensorKeyLen) return false;
  out[0] = '\0';

  // Take the last six hex digits, ignoring separators, so the same address
  // keys the same whether it arrives with colons or without.
  char hex[13];
  size_t n = 0;
  for (const char* p = mac; *p; ++p) {
    if (*p == ':' || *p == '-') continue;
    if (!isHexDigit(*p)) return false;
    if (n >= sizeof(hex) - 1) return false;
    hex[n++] = (char)tolower(*p);
  }
  if (n < 6) return false;

  memcpy(out, hex + n - 6, 6);
  out[6] = '\0';
  return true;
}

const char* dayFilePath(Epoch e, char* buf, size_t len) {
  if (!buf || len == 0) return "";
  char date[12];
  Clock::formatDate(e, date, sizeof(date));
  snprintf(buf, len, "%s/%s.csv", kLogDir, date);
  return buf;
}

bool parseDayFileName(const char* name, uint32_t& dayIndexOut) {
  if (name == nullptr) return false;
  // Skip a leading directory if we were handed a full path.
  const char* slash = strrchr(name, '/');
  if (slash) name = slash + 1;

  // Exactly "YYYY-MM-DD.csv".
  if (strlen(name) != 14) return false;
  if (name[4] != '-' || name[7] != '-') return false;
  if (strcmp(name + 10, ".csv") != 0) return false;
  for (int i = 0; i < 10; i++) {
    if (i == 4 || i == 7) continue;
    if (name[i] < '0' || name[i] > '9') return false;
  }

  DateTime dt;
  dt.year = (uint16_t)((name[0] - '0') * 1000 + (name[1] - '0') * 100 +
                       (name[2] - '0') * 10 + (name[3] - '0'));
  dt.month = (uint8_t)((name[5] - '0') * 10 + (name[6] - '0'));
  dt.day = (uint8_t)((name[8] - '0') * 10 + (name[9] - '0'));
  if (dt.month < 1 || dt.month > 12) return false;
  if (dt.day < 1 || dt.day > Clock::daysInMonth(dt.year, dt.month)) return false;

  dayIndexOut = Clock::dayIndexOf(Clock::make(dt));
  return true;
}

bool shouldRotate(const char* name, uint32_t today, uint32_t keepDays) {
  uint32_t day = 0;
  // Anything we cannot parse is not ours and is left alone. The log directory
  // is the only thing this firmware deletes from; it deletes conservatively.
  if (!parseDayFileName(name, day)) return false;
  if (keepDays == 0) return false;
  // A file dated in the future is a clock that moved backwards, not old data.
  if (day > today) return false;
  return (today - day) >= keepDays;
}

size_t formatRow(const LogRecord& r, char* buf, size_t len) {
  if (!buf || len == 0) return 0;
  if (r.key[0] == '\0') return 0;  // an unkeyed row is unattributable
  const int n = snprintf(buf, len, "%u,%s,%d,%d,%d\n", (unsigned)r.minuteOfDay,
                         r.key, (int)r.tempCx10, (int)r.humPct, (int)r.battPct);
  if (n <= 0 || (size_t)n >= len) return 0;  // truncated: write nothing
  return (size_t)n;
}

bool parseRow(const char* line, LogRecord& out) {
  if (line == nullptr) return false;
  if (*line == '#' || *line == '\0' || *line == '\n') return false;

  const char* p = line;
  long minute = 0, t = 0, h = 0, b = 0;
  if (!nextInt(p, minute)) return false;

  // The key is six hex digits between two commas, not a number - "460a4a"
  // would parse as 46 and leave the rest to shift every later field along.
  char key[kSensorKeyLen];
  size_t k = 0;
  while (*p && *p != ',') {
    if (k >= kSensorKeyLen - 1 || !isHexDigit(*p)) return false;
    key[k++] = (char)tolower(*p);
    p++;
  }
  if (k != kSensorKeyLen - 1 || *p != ',') return false;
  key[k] = '\0';
  p++;

  if (!nextInt(p, t)) return false;
  if (!nextInt(p, h)) return false;
  if (!nextInt(p, b)) return false;

  if (minute < 0 || minute > 1439) return false;

  out.minuteOfDay = (uint16_t)minute;
  memcpy(out.key, key, sizeof(key));
  out.tempCx10 = (int16_t)t;
  out.humPct = (int16_t)h;
  out.battPct = (int16_t)b;
  return true;
}

size_t formatSensorHeader(const char* key, const char* mac, const char* name,
                          char* buf, size_t len) {
  if (!buf || len == 0 || !key || !key[0]) return 0;
  const int n = snprintf(buf, len, "# sensor %s %s %s\n", key,
                         mac ? mac : "?", (name && name[0]) ? name : "-");
  if (n <= 0 || (size_t)n >= len) return 0;
  return (size_t)n;
}

// ---- DayExtremes ------------------------------------------------------------

void DayExtremes::reset(uint32_t dayIndex) {
  day = dayIndex;
  haveTemp = false;
  haveHum = false;
  tMinC = tMaxC = hMin = hMax = 0.0f;
}

void DayExtremes::observe(bool haveT, float tempC, bool haveH, float humPct) {
  if (haveT) {
    if (!haveTemp) {
      tMinC = tMaxC = tempC;
      haveTemp = true;
    } else {
      if (tempC < tMinC) tMinC = tempC;
      if (tempC > tMaxC) tMaxC = tempC;
    }
  }
  if (haveH) {
    if (!haveHum) {
      hMin = hMax = humPct;
      haveHum = true;
    } else {
      if (humPct < hMin) hMin = humPct;
      if (humPct > hMax) hMax = humPct;
    }
  }
}

void DayExtremes::observeRecord(const LogRecord& r) {
  observe(r.tempCx10 != History::kNoValue, r.tempCx10 / 10.0f,
          r.humPct != History::kNoValue, (float)r.humPct);
}

}  // namespace cc
