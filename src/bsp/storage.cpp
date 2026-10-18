#include "bsp/storage.h"

#include <FS.h>
#include <esp_timer.h>
#include <LittleFS.h>

namespace bsp {
namespace {

bool g_ready = false;
// Worth a line in the log: a fifth of Hub's 50 ms budget for one integration.
constexpr int64_t kSlowFlushUs = 10000;
File g_log;
char g_logPath[48] = {0};

}  // namespace

bool storageBegin() {
  // true = format if the mount fails, which is what a first boot needs: the
  // partition is reserved by the build but has never held a filesystem.
  g_ready = LittleFS.begin(true, "/littlefs", 10, "spiffs");
  if (!g_ready) {
    log_e("storage: LittleFS mount failed - logging disabled this boot");
    return false;
  }
  log_i("storage: LittleFS mounted, %u of %u bytes used",
        (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  return true;
}

bool storageReady() { return g_ready; }

size_t storageTotalBytes() { return g_ready ? LittleFS.totalBytes() : 0; }
size_t storageUsedBytes() { return g_ready ? LittleFS.usedBytes() : 0; }

size_t storageFreeBytes() {
  if (!g_ready) return 0;
  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}

bool ensureDir(const char* path) {
  if (!g_ready || !path) return false;
  if (LittleFS.exists(path)) return true;
  return LittleFS.mkdir(path);
}

bool removeFile(const char* path) {
  if (!g_ready || !path) return false;
  return LittleFS.remove(path);
}

bool fileExists(const char* path) {
  return g_ready && path && LittleFS.exists(path);
}

size_t fileSize(const char* path) {
  if (!g_ready || !path) return 0;
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return 0;
  const size_t n = f.size();
  f.close();
  return n;
}

void listDir(const char* path, const std::function<void(const char*)>& fn) {
  if (!g_ready || !path) return;
  File dir = LittleFS.open(path);
  if (!dir || !dir.isDirectory()) return;
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    // name() is the bare entry name on this core; callers rely on that.
    const char* n = e.name();
    const char* slash = strrchr(n, '/');
    fn(slash ? slash + 1 : n);
    e.close();
  }
  dir.close();
}

bool readLines(const char* path, size_t maxLineLen,
               const std::function<bool(const char*)>& fn) {
  if (!g_ready || !path || maxLineLen == 0) return false;
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return false;

  // One stack buffer reused for every line. Anything longer than maxLineLen is
  // a corrupt file, not a long row, so it is dropped rather than split into two
  // records that would each parse as something.
  char* buf = (char*)alloca(maxLineLen + 1);
  size_t n = 0;
  bool overlong = false;
  while (f.available()) {
    const int c = f.read();
    if (c < 0) break;
    if (c == '\n') {
      buf[n] = '\0';
      if (!overlong && n > 0 && !fn(buf)) {
        f.close();
        return true;  // the caller asked to stop; not a failure
      }
      n = 0;
      overlong = false;
      continue;
    }
    if (c == '\r') continue;
    if (n < maxLineLen) {
      buf[n++] = (char)c;
    } else {
      overlong = true;
    }
  }
  // A final line with no trailing newline - a log cut short by a power loss.
  if (!overlong && n > 0) {
    buf[n] = '\0';
    fn(buf);
  }
  f.close();
  return true;
}

// ---- the append-only log file ----

bool logOpen(const char* path) {
  if (!g_ready || !path) return false;
  logClose();
  g_log = LittleFS.open(path, FILE_APPEND, true);
  if (!g_log) {
    log_e("storage: cannot open %s for append", path);
    g_logPath[0] = '\0';
    return false;
  }
  strncpy(g_logPath, path, sizeof(g_logPath) - 1);
  g_logPath[sizeof(g_logPath) - 1] = '\0';
  return true;
}

const char* logPath() { return g_logPath; }
bool logIsOpen() { return g_ready && (bool)g_log; }

bool logAppend(const char* text, size_t len) {
  if (!logIsOpen() || !text || len == 0) return false;
  return g_log.write((const uint8_t*)text, len) == len;
}

bool logFlush() {
  if (!logIsOpen()) return false;
  // Timed, and kept. This is the one call in the logging path that reaches
  // flash, and `Hub` gives the whole integration a 50 ms budget - so when the
  // UI one day stutters once every few minutes, this says whether it was here.
  // esp_timer_get_time() rather than millis(), for the reason in
  // ARCHITECTURE.md: millis() is a 64-bit division.
  const int64_t t0 = esp_timer_get_time();
  g_log.flush();
  const int64_t us = esp_timer_get_time() - t0;
  if (us > kSlowFlushUs) {
    log_w("storage: flush took %lld ms", us / 1000);
  }
  return true;
}

void logClose() {
  if (g_log) {
    g_log.flush();
    g_log.close();
  }
  g_logPath[0] = '\0';
}

}  // namespace bsp
