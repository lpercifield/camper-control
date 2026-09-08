#pragma once
// -----------------------------------------------------------------------------
// The host stand-in for Arduino.h, used only by env:native.
//
// src/core/ is pure logic, but it still includes <Arduino.h> for two things:
// millis() and the log_* macros. This supplies both on a laptop so the core can
// be tested without a board. It is on the include path for env:native only -
// the firmware build never sees this file.
//
// The clock here is fake and deliberately so. Entity::stale() is a function of
// elapsed time, and a test that has to sleep for 15 real seconds to check a
// 15-second threshold is a test nobody runs. ccTestSetMillis() makes that
// instant and exact.
// -----------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---- Fake clock -------------------------------------------------------------
// Starts at 1, not 0. Entity tracks "never updated" as updatedMs_ == 0, so a
// zero clock would make every freshly-set entity read as never updated. On the
// board millis() has already advanced past zero long before any integration
// runs, so 1 is the honest default. See test_entity.cpp:test_epoch_zero.
inline unsigned long cc_test_millis_ = 1;

inline unsigned long millis() { return cc_test_millis_; }

inline void ccTestSetMillis(unsigned long ms) { cc_test_millis_ = ms; }
inline void ccTestAdvanceMillis(unsigned long ms) { cc_test_millis_ += ms; }

// ---- Logging ----------------------------------------------------------------
// The core logs on the paths the tests exercise (duplicate ids, a full alarm
// table). Route them to stderr so a failing test still shows the reason, and
// keep them out of stdout where Unity's own report goes.
#define log_e(fmt, ...) fprintf(stderr, "[E] " fmt "\n", ##__VA_ARGS__)
#define log_w(fmt, ...) fprintf(stderr, "[W] " fmt "\n", ##__VA_ARGS__)
#define log_i(fmt, ...) fprintf(stderr, "[I] " fmt "\n", ##__VA_ARGS__)
#define log_d(fmt, ...) fprintf(stderr, "[D] " fmt "\n", ##__VA_ARGS__)
