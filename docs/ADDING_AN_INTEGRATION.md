# Adding an integration

`ARCHITECTURE.md` explains why the seam is where it is; this is the how.

Every accessory - a relay board, a tank sender, the heater - is one class. It
owns the transport, registers entities, and never blocks.

```cpp
// src/integrations/water_pump.h
#pragma once
#include "core/entity.h"
#include "core/integration.h"

namespace cc {

class WaterPump : public Integration {
 public:
  const char* name() const override { return "Water pump"; }

  bool begin() override {
    Registry::instance().add(&pump_);
    Registry::instance().add(&pressure_);
    setLink(LinkState::Online, "local");
    return true;
  }

  void loop() override {
    if (millis() - lastMs_ < 1000) return;
    lastMs_ = millis();
    pressure_.set(readPressureSensor());
  }

 private:
  bool drive(bool on) {
    digitalWrite(PIN_PUMP_RELAY, on);
    pump_.confirm(on);   // only after the hardware actually took it
    return true;
  }

  SwitchEntity pump_{"water.pump", "Water pump", Domain::Water,
                     [this](bool on) { return drive(on); }};
  NumericEntity pressure_{"water.pressure", "Pressure", Domain::Water, "psi", 1};
  uint32_t lastMs_ = 0;
};

}  // namespace cc
```

Then one line in `setup()`:

```cpp
hub.add(&waterPump());
```

The Water page stops showing its placeholder and lists the pump and the pressure
reading. No UI code changes.

## Rules

- **`loop()` returns promptly.** Poll on a timer, drive a state machine, never
  spin waiting for a reply. Anything that genuinely must block gets its own
  FreeRTOS task and publishes results through entities.
- **`loop()` is called upwards of 20,000 times a second**, so "cheap" is not
  the same as free. A single `millis()` per pass is a real tax - it is a 64-bit
  division - and a `log_i()` on a timer inside `loop()` was enough to trip the
  50 ms guard at 66 ms. Do periodic housekeeping on a counter instead, and log
  on change rather than on a clock:

  ```cpp
  void loop() override {
    if (!newDataArrived()) {
      // ~5 Hz, and no millis() on the hot path
      if ((++tick_ & 0x0FFF) != 0) return;
      checkForTimeout();
      return;
    }
    publish();
  }
  ```
- **Confirm, do not assume.** `SwitchEntity::command()` asks the hardware;
  `confirm()` records what it actually did. A relay node that has gone offline
  should show the truth, not the last thing we wished for.
- **Entity ids are `domain.thing`** and must be unique - the registry drops
  duplicates with a warning.
- **Raise alarms by name**, clear them when the condition goes away, and prefix
  them with the integration (`"water.pump_dry"`) so a disconnect can clear the
  whole set with `clearPrefix()`.
- **Stale is handled for you.** Entities carry their own update time; the UI
  greys out anything older than `CFG_STALE_AFTER_MS`. Just stop calling `set()`
  when you lose the device.
