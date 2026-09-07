# 0005 - Format floats with snprintf, never LV_USE_FLOAT

- **Date:** 2026-09-06
- **Status:** Accepted

## Context

Values rendered on screen as a literal `f` - `f Ah f Wh`, `min f max f spread
f`, and a bare `f` under Watts. `lv_label_set_text_fmt` goes through LVGL's own
vsnprintf, which has no `%f` handler unless `LV_USE_FLOAT` is enabled: it parses
`%.0f`, consumes the `%.0`, and emits the `f` literally.

Meanwhile Volts, Amps and Battery rendered correctly, because they already went
through `Entity::format(buf, len)`, which uses real `snprintf`.

## Decision

Format floating-point values with `snprintf` into a buffer and call
`lv_label_set_text`. Prefer `Entity::format()` where an entity exists, since it
carries the unit and precision. `LV_USE_FLOAT` stays 0.

## Alternatives rejected

**`LV_USE_FLOAT 1`.** The one-line fix, and it does make `%f` work. Rejected
because that flag also retypes `lv_value_precise_t` from `int32_t` to `float`
across `lv_area.h`, `lv_draw_arc.h` and `lv_obj_property.h` - it changes LVGL's
geometry ABI to fix a string. Any config flag that appears in a struct-defining
header is not a formatting switch. The comment directly beneath it in
`lv_conf.h` says `LV_USE_MATRIX` *requires* it, which is the tell.

## Consequences

Slightly more verbose at call sites, and every new float readout must remember
to use it. `Entity::format()` already makes that the path of least resistance
for anything backed by an entity; derived values such as watt-hours are the
exception and need `snprintf` directly.
