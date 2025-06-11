//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>
#include <ranges>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>

using oxygen::Component;
using oxygen::Composition;
using oxygen::TypeId;
using ComponentsCollection = std::vector<std::unique_ptr<Component>>;

struct Composition::ComponentManager {
  ComponentsCollection components_;
  std::unordered_map<TypeId, size_t> component_index_;
};

Composition::~Composition() noexcept { DestroyComponents(); }

auto Composition::HasComponents() const noexcept -> bool
{
  return pimpl_ && !pimpl_->components_.empty();
}

// ReSharper disable once CppMemberFunctionMayBeConst
void Composition::DestroyComponents() noexcept
{
  if (!pimpl_) {
    return;
  }
  // Clear in reverse order - dependents before dependencies
  auto& components = pimpl_->components_;
  while (!components.empty()) {
    // Absorb all exceptions
    try {
      components.pop_back(); // unique_ptr auto-destructs
    } catch (const std::exception& e) {
      LOG_F(
        ERROR, "Exception caught while destructing components: %s", e.what());
    }
  }
}

Composition::Composition(const Composition& other)
  : pimpl_(other.pimpl_)
{
  DCHECK_NOTNULL_F(
    pimpl_, "Composition copy constructed from a moved composition!");
}

auto Composition::operator=(const Composition& other) -> Composition&
{
  DCHECK_NOTNULL_F(other.pimpl_, "Composition used after having been moved!");
  if (this != &other) {
    pimpl_ = other.pimpl_;
  }
  return *this;
}

Composition::Composition(Composition&& other) noexcept
  : pimpl_(std::move(other.pimpl_))
{
  DCHECK_NOTNULL_F(pimpl_, "Composition constructed from a moved composition!");
}

auto Composition::operator=(Composition&& other) noexcept -> Composition&
{
  DCHECK_NOTNULL_F(other.pimpl_, "Composition used after having been moved!");
  if (this != &other) {
    pimpl_ = std::move(other.pimpl_);
  }
  return *this;
}

Composition::Composition()
  : pimpl_(std::make_shared<ComponentManager>())
{
  DCHECK_NOTNULL_F(pimpl_, "Failed to allocate component manager");
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
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  DCHECK_F(!dependencies.empty(), "Dependencies must not be empty");

  for (const auto dep_id : dependencies) {
    if (!pimpl_->component_index_.contains(dep_id)) {
      throw ComponentError("Missing dependency component");
    }
  }
}

auto Composition::ExpectExistingComponent(const TypeId id) const -> bool
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  if (!pimpl_->component_index_.contains(id)) {
    DLOG_F(
      WARNING, "Attempt to remove or replace a component that is not there");
    return false;
  }
  return true;
}

auto Composition::HasComponentImpl(const TypeId id) const -> bool
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  return pimpl_->component_index_.contains(id);
}

auto Composition::AddComponentImpl(std::unique_ptr<Component> component) const
  -> Component&
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  DCHECK_NOTNULL_F(component, "Component must not be null");
  DCHECK_F(!pimpl_->component_index_.contains(component->GetTypeId()),
    "Component already exists");

  pimpl_->components_.emplace_back(std::move(component));
  const auto& entry = pimpl_->components_.back();
  pimpl_->component_index_[entry->GetTypeId()] = pimpl_->components_.size() - 1;

  return *entry;
}

auto Composition::ReplaceComponentImpl(const TypeId old_id,
  std::unique_ptr<Component> new_component) const -> Component&
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  DCHECK_NOTNULL_F(new_component, "Component must not be null");
  DCHECK_F(
    pimpl_->component_index_.contains(old_id), "Old component must exist");

  const auto new_id = new_component->GetTypeId();
  const auto index = pimpl_->component_index_.at(old_id);
  pimpl_->components_[index] = std::move(new_component);
  if (old_id != new_id) {
    pimpl_->component_index_.erase(old_id);
    pimpl_->component_index_[new_id] = index;
  }
  return *pimpl_->components_[index];
}

auto Composition::GetComponentImpl(const TypeId id) const -> Component&
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  const auto it = pimpl_->component_index_.find(id);
  if (it == pimpl_->component_index_.end()) {
    throw ComponentError("Missing dependency component");
  }
  return *pimpl_->components_[it->second];
}

void Composition::RemoveComponentImpl(
  const TypeId id, const bool update_indices) const
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  const auto index = pimpl_->component_index_.at(id);
  pimpl_->components_.erase(pimpl_->components_.begin()
    + static_cast<ComponentsCollection::difference_type>(index));
  pimpl_->component_index_.erase(id);

  if (update_indices) {
    const auto size = pimpl_->components_.size();
    for (size_t i = index; i < size; ++i) {
      pimpl_->component_index_[pimpl_->components_[i]->GetTypeId()] = i;
    }
  }
}

void Composition::DeepCopyComponentsFrom(const Composition& other)
{
  DCHECK_NOTNULL_F(
    other.pimpl_, "Composition deep copied from a moved composition!");
  pimpl_ = std::make_shared<ComponentManager>();
  for (const auto& entry : other.pimpl_->components_) {
    if (const auto* comp = entry.get(); comp->IsCloneable()) {
      pimpl_->components_.emplace_back(comp->Clone());
      const auto& clone = pimpl_->components_.back();
      clone->UpdateDependencies(other);
      pimpl_->component_index_[clone->GetTypeId()]
        = pimpl_->components_.size() - 1;
    } else {
      throw ComponentError("Component must be cloneable");
    }
  }
}

auto Composition::IsComponentRequired(const TypeId id) const -> bool
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  for (const auto& comp : pimpl_->components_) {
    if (std::ranges::find(comp->Dependencies(), id)
      != comp->Dependencies().end()) {
      return true;
    }
  }
  return false;
}

template <typename ValueType>
auto Composition::Iterator<ValueType>::operator*() const -> reference
{
  return *mgr_->components_[pos_];
}

template <typename ValueType>
auto Composition::Iterator<ValueType>::operator->() const -> pointer
{
  return mgr_->components_[pos_].get();
}

template <typename ValueType>
auto Composition::Iterator<ValueType>::operator++() -> Iterator&
{
  ++pos_;
  return *this;
}

template <typename ValueType>
auto Composition::Iterator<ValueType>::operator++(int) -> Iterator
{
  Iterator tmp = *this;
  ++*this;
  return tmp;
}

template <typename ValueType>
auto Composition::Iterator<ValueType>::operator==(const Iterator& rhs) const
  -> bool
{
  return pos_ == rhs.pos_;
}

template <typename ValueType>
auto Composition::Iterator<ValueType>::operator!=(const Iterator& rhs) const
  -> bool
{
  return !(*this == rhs);
}

// Force instantiation of required template specializations
template class Composition::Iterator<Component>;
template class Composition::Iterator<const Component>;

auto Composition::begin() -> iterator
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  return { pimpl_.get(), 0 };
}

auto Composition::end() -> iterator
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  return { pimpl_.get(), pimpl_->components_.size() };
}

auto Composition::begin() const -> const_iterator
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  return { pimpl_.get(), 0 };
}

auto Composition::end() const -> const_iterator
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  return { pimpl_.get(), pimpl_->components_.size() };
}

auto Composition::cbegin() const -> const_iterator
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  return { pimpl_.get(), 0 };
}

auto Composition::cend() const -> const_iterator
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  return { pimpl_.get(), pimpl_->components_.size() };
}

void Composition::PrintComponents(std::ostream& out) const
{
  DCHECK_NOTNULL_F(pimpl_, "Composition used after having been moved!");
  std::string object_name = "Unknown";
  if (HasComponent<ObjectMetaData>()) {
    object_name = GetComponent<ObjectMetaData>().GetName();
  }

  const auto& components_ = pimpl_->components_;
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
