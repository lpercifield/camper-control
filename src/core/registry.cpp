#include "core/registry.h"
#include <string.h>

namespace cc {

Registry& Registry::instance() {
  static Registry r;
  return r;
}

void Registry::add(Entity* e) {
  if (!e) return;
  if (find(e->id()) != nullptr) {
    log_w("registry: duplicate entity id '%s' ignored", e->id());
    return;
  }
  entities_.push_back(e);
}

Entity* Registry::find(const char* id) const {
  if (!id) return nullptr;
  for (Entity* e : entities_) {
    if (strcmp(e->id(), id) == 0) return e;
  }
  return nullptr;
}

std::vector<Entity*> Registry::inDomain(Domain d) const {
  std::vector<Entity*> out;
  for (Entity* e : entities_) {
    if (e->domain() == d) out.push_back(e);
  }
  return out;
}

size_t Registry::countInDomain(Domain d) const {
  size_t n = 0;
  for (Entity* e : entities_) {
    if (e->domain() == d) n++;
  }
  return n;
}

}  // namespace cc
