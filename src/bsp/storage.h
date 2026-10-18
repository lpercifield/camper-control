#pragma once
#include <Arduino.h>
#include <functional>

// -----------------------------------------------------------------------------
// The flash filesystem the sensor log lives on.
//
// This is here rather than in src/core/ for the same reason settings.cpp is:
// it is the board. LittleFS on the `spiffs` data partition that
// default_8MB.csv already reserves (1.5 MB at 0x670000) and nothing else uses.
//
// **Writes happen on the main loop only.** A flash write disables the cache,
// and this board panics if a cached-memory ISR fires inside that window - that
// is the whole of decisions/0004. The same rule Settings follows, for the same
// reason, plus one of its own: `Hub` gives every integration a 50 ms budget and
// an erase can spend most of it.
// -----------------------------------------------------------------------------

namespace bsp {

// Mounts, formatting on first boot. False means the log is simply unavailable -
// the firmware carries on without it rather than refusing to run.
bool storageBegin();
bool storageReady();

size_t storageTotalBytes();
size_t storageUsedBytes();
size_t storageFreeBytes();

bool ensureDir(const char* path);
bool removeFile(const char* path);
bool fileExists(const char* path);
size_t fileSize(const char* path);

// Calls back with each entry name in a directory. Names only, no paths.
void listDir(const char* path, const std::function<void(const char*)>& fn);

// Reads a file line by line. The callback gets a NUL-terminated line with the
// newline stripped; returning false stops the walk early.
bool readLines(const char* path, size_t maxLineLen,
               const std::function<bool(const char*)>& fn);

// ---- the append-only log file ----
// One file is held open, because opening and closing per row costs a directory
// walk and a metadata write every time. Reopening is only needed when the day
// rolls over.
bool logOpen(const char* path);
const char* logPath();
bool logIsOpen();
// Buffered: LittleFS holds it until a block fills or logFlush() is called.
bool logAppend(const char* text, size_t len);
// Pushes it to flash. This is the call that can actually block - measure it
// before changing how often it happens.
bool logFlush();
void logClose();

}  // namespace bsp
