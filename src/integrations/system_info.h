#pragma once
#include <vector>

#include "core/entity.h"
#include "core/integration.h"

namespace cc {

// The controller reporting on itself: uptime, free heap and its low water
// mark, the LVGL pool, the loop and flush rates, what answered on the I2C bus,
// how often the phone broadcast goes out, and one row per integration saying
// whether its link is up.
//
// **It owns no device.** Every other integration owns a thing in the van and
// publishes what that thing is doing. This one is the other diagnostic
// direction from `BleBroadcast`: that one reads the registry out to the radio,
// this one fills the registry from the chip. Both are Integrations for the
// same concrete reason - `Hub` gives them a loop slice and names them if they
// ever hold it past 50 ms.
//
// Every figure here was readable only over a serial cable until this existed,
// which is a poor way to diagnose a van parked on a driveway. The heartbeat in
// `bsp::displayLoop()` still logs, because the protocol that needs it most
// starts with a blank screen, and a blank screen shows no overlay either.
//
// Nothing here is a *measurement of the van*, so none of it goes in the phone
// broadcast: `ICD.md` is about what the battery and the sensors are doing, and
// the advertisement has three spare bytes of thirty-one.
class SystemInfo : public Integration {
 public:
  const char* name() const override { return "System"; }
  bool begin() override;
  void loop() override;

 private:
  // One second of counters is tens of thousands of loops, so the rates are
  // already stable at this window; it is chosen so the uptime's seconds field
  // ticks like a clock rather than in jumps.
  static constexpr uint32_t kSampleMs = 1000;

  // The phone broadcast gets its own, longer window, because it is the one
  // counter here that moves slower than the sample does. At a 2 s refresh a
  // one-second window can only ever see nought or one frame, so the figure
  // read 0 and 60 alternately - measured on hardware before this existed. Ten
  // seconds is five frames, which is a stable 30/min.
  static constexpr uint32_t kFrameWindowMs = 10000;

  // Link rows are capped because the ids have to be literals - see
  // kLinkIds in the implementation. Past this an integration simply gets no
  // row; the header still shows the worst link in the van.
  static constexpr size_t kMaxLinks = 6;

  void sample(uint32_t nowMs);

  TextEntity uptime_{"system.uptime", "Uptime", Domain::System};
  NumericEntity heap_{"system.heap", "Free heap", Domain::System, "KB", 1};
  // The low water mark is the one that says whether the van nearly died while
  // nobody was looking. Free heap alone recovers and tells you nothing.
  NumericEntity heapMin_{"system.heap_min", "Heap low water", Domain::System,
                         "KB", 1};
  // decisions/0013: exhausting this pool does not report itself, so it is
  // worth a row of its own rather than a line in the boot log.
  NumericEntity pool_{"system.lvgl_pool", "LVGL pool used", Domain::System,
                      "%", 0};
  // The rate goes in the label rather than in the unit, because "19875 /s"
  // with the space NumericEntity::format() puts there reads worse than a bare
  // number under a column that says what it counts.
  NumericEntity loopRate_{"system.loop_rate", "Loops/sec", Domain::System, "",
                          0};
  NumericEntity flushRate_{"system.flush_rate", "LVGL flushes/sec",
                           Domain::System, "", 0};
  NumericEntity bleRate_{"system.ble_frames", "Phone frames/min",
                         Domain::System, "", 0};
  TextEntity i2c_{"system.i2c", "I2C bus", Domain::System};

  // Built in begin(), once Hub order is known, and never touched again.
  // **Reserved exactly once**: the registry holds non-owning pointers
  // (`ARCHITECTURE.md`), so a reallocation here would dangle every one of
  // them.
  std::vector<TextEntity> links_;

  uint64_t uptimeMs_ = 0;  // 64-bit, so the 49-day millis() wrap is a non-event
  uint16_t skip_ = 0;      // loop() looks at the clock every 256th pass
  uint32_t lastSampleMs_ = 0;
  uint32_t lastLoops_ = 0;
  uint32_t lastFlushes_ = 0;
  uint32_t lastFrames_ = 0;
  uint32_t lastFrameSampleMs_ = 0;
};

SystemInfo& systemInfo();

}  // namespace cc
