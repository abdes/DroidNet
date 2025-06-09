//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Scene/SceneFlags.h>

namespace oxygen::scene::detail {

//! Add a brief description of the GraphData class
/*!
 The implementation uses an intrusive linked list structure for sibling
 relationships and direct handle references for parent-child links, providing
 O(1) hierarchy operations while maintaining memory efficiency. Transform
 updates follow a hierarchical dependency model with lazy evaluation and dirty
 tracking.
*/
class GraphData final : public Component {
  OXYGEN_COMPONENT(GraphData)
public:
  GraphData() = default;
  ~GraphData() override = default;
  OXYGEN_DEFAULT_COPYABLE(GraphData)
  OXYGEN_DEFAULT_MOVABLE(GraphData)

  [[nodiscard]] auto GetParent() const noexcept -> const ResourceHandle&
  {
    return parent_;
  }
  [[nodiscard]] auto GetFirstChild() const noexcept -> const ResourceHandle&
  {
    return first_child_;
  }
  [[nodiscard]] auto GetNextSibling() const noexcept -> const ResourceHandle&
  {
    return next_sibling_;
  }
  [[nodiscard]] auto GetPrevSibling() const noexcept -> const ResourceHandle&
  {
    return prev_sibling_;
  }
  void SetParent(const ResourceHandle& parent) noexcept { parent_ = parent; }
  void SetFirstChild(const ResourceHandle& child) noexcept
  {
    first_child_ = child;
  }
  void SetNextSibling(const ResourceHandle& sibling) noexcept
  {
    next_sibling_ = sibling;
  }
  void SetPrevSibling(const ResourceHandle& sibling) noexcept
  {
    prev_sibling_ = sibling;
  }

  [[nodiscard]] auto IsCloneable() const noexcept -> bool override
  {
    return true;
  }
  [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
  {
    // Create an orphaned clone (no hierarchy relationships)
    // This is the safest approach since handles may not be valid in clone
    // context
    return std::make_unique<GraphData>();
  }

private:
  ResourceHandle parent_;
  ResourceHandle first_child_;
  ResourceHandle next_sibling_;
  ResourceHandle prev_sibling_;
};

} // namespace oxygen::scene::detail
