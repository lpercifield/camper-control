#include "core/settings.h"

#include <string.h>

namespace cc {
namespace {
constexpr const char* kNamespace = "camper";
constexpr const char* kKeyBmsMac = "bms_mac";
constexpr const char* kKeyBright = "bright";
constexpr const char* kKeyTimeout = "timeout";

// NVS keys are limited to 15 characters, so a MAC cannot be stored with its
// colons: "n" plus twelve hex digits is thirteen, which fits with room to
// spare. Anything longer is silently truncated by NVS, which would make two
// sensors share a name.
bool sensorKey(const char* mac, char* out, size_t len) {
  if (mac == nullptr || len < 14) return false;
  size_t n = 0;
  out[n++] = 'n';
  for (const char* p = mac; *p && n < 13; ++p) {
    if (*p == ':') continue;
    out[n++] = static_cast<char>(tolower(*p));
  }
  out[n] = '\0';
  return n == 13;
}

}  // namespace

bool Settings::sensorName(const char* mac, char* out, size_t len) {
  if (out == nullptr || len == 0) return false;
  out[0] = '\0';
  char key[16];
  if (!open_ || !sensorKey(mac, key, sizeof(key))) return false;
  prefs_.getString(key, out, len);
  return out[0] != '\0';
}

void Settings::setSensorName(const char* mac, const char* name) {
  char key[16];
  if (!open_ || !sensorKey(mac, key, sizeof(key))) return;
  if (name == nullptr || name[0] == '\0') {
    prefs_.remove(key);
    log_i("settings: sensor %s name cleared", mac);
    return;
  }
  char trimmed[kSensorNameLen];
  strncpy(trimmed, name, sizeof(trimmed));
  trimmed[sizeof(trimmed) - 1] = '\0';
  prefs_.putString(key, trimmed);
  log_i("settings: sensor %s named '%s'", mac, trimmed);
}

void Settings::clearSensorName(const char* mac) { setSensorName(mac, nullptr); }

Settings& Settings::instance() {
  static Settings s;
  return s;
}

void Settings::begin() {
  open_ = prefs_.begin(kNamespace, false);
  if (!open_) {
    log_e("settings: NVS unavailable, running on defaults this boot");
    return;
  }
  prefs_.getString(kKeyBmsMac, bmsMac_, sizeof(bmsMac_));
  brightness_ = prefs_.getUChar(kKeyBright, CFG_BACKLIGHT_DEFAULT);
  timeoutMs_ = prefs_.getULong(kKeyTimeout, CFG_BACKLIGHT_IDLE_MS);

  log_i("settings: bms='%s' brightness=%u timeout=%lums",
        hasBmsMac() ? bmsMac_ : "(none)", (unsigned)brightness_,
        (unsigned long)timeoutMs_);
}

void Settings::loop() {
  if (!pendingMac_) return;
  // Copy before clearing the flag: the task only writes the buffer while the
  // flag is false, so this ordering means we never read a half-written address.
  char mac[kMacLen];
  strncpy(mac, pendingMacBuf_, sizeof(mac));
  mac[sizeof(mac) - 1] = '\0';
  pendingMac_ = false;

  if (strcmp(mac, bmsMac_) == 0) return;  // already what we have; no flash write
  strncpy(bmsMac_, mac, sizeof(bmsMac_));
  bmsMac_[sizeof(bmsMac_) - 1] = '\0';
  if (open_) prefs_.putString(kKeyBmsMac, bmsMac_);
  log_i("settings: remembered BMS %s", bmsMac_);
}

void Settings::rememberBmsMacFromTask(const char* mac) {
  if (!mac || !*mac) return;
  if (pendingMac_) return;  // a handover is already in flight
  strncpy(pendingMacBuf_, mac, sizeof(pendingMacBuf_));
  pendingMacBuf_[sizeof(pendingMacBuf_) - 1] = '\0';
  __sync_synchronize();  // the buffer must be visible before the flag
  pendingMac_ = true;
}

void Settings::clearBmsMac() {
  bmsMac_[0] = '\0';
  if (open_) prefs_.remove(kKeyBmsMac);
  log_i("settings: forgot the saved BMS");
}

void Settings::setBrightness(uint8_t level, bool persist) {
  brightness_ = level;
  if (persist) commitBrightness();
}

void Settings::commitBrightness() {
  if (open_) prefs_.putUChar(kKeyBright, brightness_);
}

void Settings::setScreenTimeoutMs(uint32_t ms) {
  timeoutMs_ = ms;
  if (open_) prefs_.putULong(kKeyTimeout, ms);
}

}  // namespace cc
