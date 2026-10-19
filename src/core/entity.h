#pragma once
#include <Arduino.h>
#include <functional>
#include "config.h"

// -----------------------------------------------------------------------------
// Entities are the things the UI knows how to draw: one reading, one switch,
// one status flag. Integrations create them and push values in; screens and
// alarm rules read them out. Nothing in the UI ever talks to a driver directly.
// -----------------------------------------------------------------------------

namespace cc {

enum class Domain : uint8_t { Power, Lighting, Water, Climate, System, COUNT };

enum class EntityKind : uint8_t { Numeric, Binary, Switch, Text };

const char* domainName(Domain d);

class Entity {
 public:
  Entity(const char* id, const char* name, Domain domain, EntityKind kind);
  virtual ~Entity() = default;

  const char* id() const { return id_; }
  const char* name() const { return name_; }
  Domain domain() const { return domain_; }
  EntityKind kind() const { return kind_; }

  uint32_t updatedMs() const { return updatedMs_; }
  bool everUpdated() const { return updatedMs_ != 0; }
  bool stale() const {
    return !everUpdated() || (millis() - updatedMs_) > CFG_STALE_AFTER_MS;
  }
  void markUpdated() { updatedMs_ = millis(); }

  // Formatted for display, e.g. "13.42 V" or "ON". Never returns null.
  virtual const char* format(char* buf, size_t len) const = 0;

 protected:
  const char* id_;
  const char* name_;
  Domain domain_;
  EntityKind kind_;
  uint32_t updatedMs_ = 0;
};

// ---- A measured number ------------------------------------------------------
class NumericEntity : public Entity {
 public:
  NumericEntity(const char* id, const char* name, Domain domain,
                const char* unit, uint8_t decimals = 1);

  void set(float v);
  float value() const { return value_; }
  // Virtual so a temperature can answer with the unit it is actually being
  // drawn in rather than the one it is stored in.
  virtual const char* unit() const { return unit_; }
  uint8_t decimals() const { return decimals_; }

  const char* format(char* buf, size_t len) const override;

 private:
  float value_ = 0.0f;
  const char* unit_;
  uint8_t decimals_;
};

// ---- A temperature ----------------------------------------------------------
// Always *stored* in Celsius, whatever the screen says. value() is therefore
// comparable against a threshold no matter which unit the crew picked, and
// format() is the only place the conversion happens. See core/units.h.
class TemperatureEntity : public NumericEntity {
 public:
  TemperatureEntity(const char* id, const char* name, Domain domain,
                    uint8_t decimals = 1);

  // The unit on screen, not the unit in the field: "°C" or "°F".
  const char* unit() const override;
  const char* format(char* buf, size_t len) const override;
};

// ---- A read-only true/false -------------------------------------------------
class BinaryEntity : public Entity {
 public:
  BinaryEntity(const char* id, const char* name, Domain domain,
               const char* trueText = "ON", const char* falseText = "OFF");

  void set(bool v);
  bool value() const { return value_; }

  const char* format(char* buf, size_t len) const override;

 protected:
  BinaryEntity(const char* id, const char* name, Domain domain, EntityKind kind,
               const char* trueText, const char* falseText);
  bool value_ = false;
  const char* trueText_;
  const char* falseText_;
};

// ---- Something we can actually switch ---------------------------------------
// The integration supplies the writer; it returns true if the command was
// accepted. State is only updated when the integration confirms it, so a relay
// node that has gone offline shows the truth rather than what we wished for.
class SwitchEntity : public BinaryEntity {
 public:
  using Writer = std::function<bool(bool)>;

  SwitchEntity(const char* id, const char* name, Domain domain, Writer writer,
               const char* trueText = "ON", const char* falseText = "OFF");

  bool command(bool on);
  void confirm(bool on) { set(on); }

 private:
  Writer writer_;
};

// ---- A line of text ---------------------------------------------------------
// For a reading that is neither a number nor a flag: an uptime, what answered
// on the I2C bus, an integration's own words about its link.
//
// It **owns** its text rather than holding a pointer, which is the opposite of
// id_ and name_. Those are compile-time literals; a value is built at runtime
// by whoever publishes it, usually into a stack buffer, and a borrowed pointer
// would have to outlive the entity. Nothing that wants one of these can
// promise that.
class TextEntity : public Entity {
 public:
  // Long enough for "4d 02:17:33" and a short status line. Anything longer is
  // truncated rather than refused: a row 456 px wide cannot draw it either.
  static constexpr size_t kMaxLen = 32;

  TextEntity(const char* id, const char* name, Domain domain);

  void set(const char* text);
  const char* value() const { return text_; }

  const char* format(char* buf, size_t len) const override;

 private:
  char text_[kMaxLen] = {0};
};

}  // namespace cc
