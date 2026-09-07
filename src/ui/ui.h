#pragma once
#include <Arduino.h>
#include "core/entity.h"

namespace cc {
namespace ui {

// Builds the screen once LVGL and the integrations are up.
void begin();

// Refreshes the visible page from entity values. Cheap to call every loop; it
// rate-limits itself internally.
void tick();

void showDomain(Domain d);

}  // namespace ui
}  // namespace cc
