#include "core/settings.h"

#include <string.h>

namespace cc {
namespace {
constexpr const char* kNamespace = "camper";
constexpr const char* kKeyBmsMac = "bms_mac";
constexpr const char* kKeyBright = "bright";
constexpr const char* kKeyTimeout = "timeout";
}  // namespace

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
