#include "core/entity.h"

namespace cc {

const char* domainName(Domain d) {
  switch (d) {
    case Domain::Power:    return "Power";
    case Domain::Lighting: return "Lights";
    case Domain::Water:    return "Water";
    case Domain::Climate:  return "Climate";
    case Domain::System:   return "System";
    default:               return "?";
  }
}

Entity::Entity(const char* id, const char* name, Domain domain, EntityKind kind)
    : id_(id), name_(name), domain_(domain), kind_(kind) {}

// ---- NumericEntity ----------------------------------------------------------

NumericEntity::NumericEntity(const char* id, const char* name, Domain domain,
                             const char* unit, uint8_t decimals)
    : Entity(id, name, domain, EntityKind::Numeric),
      unit_(unit ? unit : ""),
      decimals_(decimals) {}

void NumericEntity::set(float v) {
  value_ = v;
  markUpdated();
}

const char* NumericEntity::format(char* buf, size_t len) const {
  if (!buf || len == 0) return "";
  if (!everUpdated()) {
    snprintf(buf, len, "--");
  } else if (unit_[0]) {
    snprintf(buf, len, "%.*f %s", decimals_, value_, unit_);
  } else {
    snprintf(buf, len, "%.*f", decimals_, value_);
  }
  return buf;
}

// ---- BinaryEntity -----------------------------------------------------------

BinaryEntity::BinaryEntity(const char* id, const char* name, Domain domain,
                           const char* trueText, const char* falseText)
    : Entity(id, name, domain, EntityKind::Binary),
      trueText_(trueText),
      falseText_(falseText) {}

BinaryEntity::BinaryEntity(const char* id, const char* name, Domain domain,
                           EntityKind kind, const char* trueText,
                           const char* falseText)
    : Entity(id, name, domain, kind),
      trueText_(trueText),
      falseText_(falseText) {}

void BinaryEntity::set(bool v) {
  value_ = v;
  markUpdated();
}

const char* BinaryEntity::format(char* buf, size_t len) const {
  if (!buf || len == 0) return "";
  snprintf(buf, len, "%s", !everUpdated() ? "--" : (value_ ? trueText_ : falseText_));
  return buf;
}

// ---- SwitchEntity -----------------------------------------------------------

SwitchEntity::SwitchEntity(const char* id, const char* name, Domain domain,
                           Writer writer, const char* trueText,
                           const char* falseText)
    : BinaryEntity(id, name, domain, EntityKind::Switch, trueText, falseText),
      writer_(std::move(writer)) {}

bool SwitchEntity::command(bool on) {
  if (!writer_) return false;
  return writer_(on);
}

}  // namespace cc
