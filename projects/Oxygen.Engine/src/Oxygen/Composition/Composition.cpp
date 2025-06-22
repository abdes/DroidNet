//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>
#include <ranges>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>

using oxygen::Component;
using oxygen::Composition;
using oxygen::TypeId;

Composition::~Composition() noexcept
{
  LOG_SCOPE_FUNCTION(3);
  DestroyComponents();
}

auto Composition::HasComponents() const noexcept -> bool
{
  std::shared_lock lock(mutex_);
  return !components_.empty();
}

void Composition::DestroyComponents() noexcept
{
  std::unique_lock lock(mutex_);
  // Clear in reverse order - dependents before dependencies
  while (!components_.empty()) {
    // Absorb all exceptions
    try {
      components_.pop_back();
    } catch (const std::exception& e) {
      LOG_F(
        ERROR, "Exception caught while destructing components: %s", e.what());
    }
  }
}

Composition::Composition(const Composition& other)
  : components_(other.components_)
{
  // Shallow copy: share the same component instances
}

auto Composition::operator=(const Composition& other) -> Composition&
{
  if (this != &other) {
    // Shallow copy: share the same component instances
    components_ = other.components_;
  }
  return *this;
}

Composition::Composition(Composition&& other) noexcept
  : components_(std::move(other.components_))
{
}

auto Composition::operator=(Composition&& other) noexcept -> Composition&
{
  if (this != &other) {
    DestroyComponents();
    components_ = std::move(other.components_);
  }
  return *this;
}

Composition::Composition(std::size_t initial_capacity)
  : components_()
{
  // Reserve capacity to prevent reallocations that would invalidate pointers
  // stored during UpdateDependencies calls
  components_.reserve(initial_capacity);
}

void Composition::ValidateDependencies(
  const TypeId comp_id, const std::span<const TypeId> dependencies)
{
  DCHECK_F(!dependencies.empty(), "Dependencies must not be empty");

  for (size_t i = 0; i < dependencies.size(); ++i) {
    const auto dep_id = dependencies[i];

    // Check for self-dependency
    if (dep_id == comp_id) {
      throw ComponentError("Component cannot depend on itself");
    }

    // Check for duplicates by comparing with previous elements
    for (size_t j = 0; j < i; ++j) {
      if (dependencies[j] == dep_id) {
        throw ComponentError("Duplicate dependency detected");
      }
    }
  }
}

void Composition::EnsureDependencies(
  const std::span<const TypeId> dependencies) const
{
  DCHECK_F(!dependencies.empty(), "Dependencies must not be empty");

  for (const auto dep_id : dependencies) {
    auto it = std::ranges::find_if(components_,
      [dep_id](const auto& comp) { return comp->GetTypeId() == dep_id; });
    if (it == components_.end()) {
      throw ComponentError("Missing dependency component");
    }
  }
}

auto Composition::ExpectExistingComponent(const TypeId id) const -> bool
{
  if (!std::ranges::any_of(components_,
        [id](const auto& comp) { return comp->GetTypeId() == id; })) {
    DLOG_F(
      WARNING, "Attempt to remove or replace a component that is not there");
    return false;
  }
  return true;
}

auto Composition::HasComponentImpl(const TypeId id) const -> bool
{
  return std::ranges::any_of(
    components_, [id](const auto& comp) { return comp->GetTypeId() == id; });
}

auto Composition::AddComponentImpl(std::shared_ptr<Component> component)
  -> Component&
{
  DCHECK_NOTNULL_F(component, "Component must not be null");
  DCHECK_F(
    !HasComponentImpl(component->GetTypeId()), "Component already exists");

  components_.emplace_back(std::move(component));
  return *components_.back();
}

auto Composition::ReplaceComponentImpl(
  const TypeId old_id, std::shared_ptr<Component> new_component) -> Component&
{
  DCHECK_NOTNULL_F(new_component, "Component must not be null");
  auto it = std::ranges::find_if(components_,
    [old_id](const auto& comp) { return comp->GetTypeId() == old_id; });
  DCHECK_F(it != components_.end(), "Old component must exist");

  *it = std::move(new_component);
  return **it;
}

auto Composition::GetComponentImpl(const TypeId id) const -> Component&
{
  auto it = std::ranges::find_if(
    components_, [id](const auto& comp) { return comp->GetTypeId() == id; });
  if (it == components_.end()) {
    throw ComponentError("Missing dependency component");
  }
  return **it;
}

void Composition::RemoveComponentImpl(const TypeId id)
{
  auto it = std::ranges::find_if(
    components_, [id](const auto& comp) { return comp->GetTypeId() == id; });
  if (it != components_.end()) {
    components_.erase(it);
  }
}

void Composition::DeepCopyComponentsFrom(const Composition& other)
{
  std::unique_lock lock(mutex_);

  // Resize to fit the actual number of components being copied
  const auto component_count = other.components_.size();
  components_.clear();
  components_.reserve(component_count);
  for (const auto& entry : other.components_) {
    if (const Component* comp = entry.get(); comp->IsCloneable()) {
      components_.emplace_back(std::shared_ptr<Component>(comp->Clone()));
    } else {
      throw ComponentError("Component must be cloneable");
    }
  }

  // Update dependencies AFTER all components are added to prevent invalidation
  for (const auto& comp : components_) {
    if (comp->HasDependencies()) {
      comp->UpdateDependencies([this](TypeId id) -> Component& {
        // Nollocking needed here
        return GetComponentImpl(id);
      });
    }
  }
}

auto Composition::IsComponentRequired(const TypeId id) const -> bool
{
  for (const auto& comp : components_) {
    if (std::ranges::find(comp->Dependencies(), id)
      != comp->Dependencies().end()) {
      return true;
    }
  }
  return false;
}

void Composition::PrintComponents(std::ostream& out) const
{
  std::shared_lock lock(mutex_);

  std::string object_name = "Unknown";
  if (HasComponent<ObjectMetaData>()) {
    object_name = static_cast<const ObjectMetaData&>(
      GetComponentImpl(ObjectMetaData::ClassTypeId()))
                    .GetName();
  }

  out << "> Object \"" << object_name << "\" has " << components_.size()
      << " components:\n";
  for (const auto& entry : components_) {
    out << "   [" << entry->GetTypeId() << "] " << entry->GetTypeName();
    if (!entry->Dependencies().empty()) {
      out << " << Requires: ";
      for (size_t i = 0; i < entry->Dependencies().size(); ++i) {
        const auto& dep_component = GetComponentImpl(entry->Dependencies()[i]);
        out << dep_component.GetTypeName();
        if (i < entry->Dependencies().size() - 1) {
          out << ", ";
        }
      }
    }
    out << "\n";
  }
  out << "\n";
}
