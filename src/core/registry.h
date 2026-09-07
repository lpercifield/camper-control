#pragma once
#include <vector>
#include "core/entity.h"

namespace cc {

// The one place that knows every entity in the van. Integrations add to it at
// boot; screens iterate it. Adding a new accessory later means writing an
// integration that registers entities here - no UI code has to change.
class Registry {
 public:
  static Registry& instance();

  void add(Entity* e);
  Entity* find(const char* id) const;
  size_t size() const { return entities_.size(); }
  Entity* at(size_t i) const { return entities_[i]; }

  // Entities in one domain, in registration order.
  std::vector<Entity*> inDomain(Domain d) const;
  size_t countInDomain(Domain d) const;

 private:
  Registry() = default;
  std::vector<Entity*> entities_;
};

}  // namespace cc
