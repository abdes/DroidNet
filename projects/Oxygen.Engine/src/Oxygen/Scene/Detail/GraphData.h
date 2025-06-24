//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

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

  [[nodiscard]] auto GetParent() const noexcept -> const NodeHandle&
  {
    return parent_;
  }
  [[nodiscard]] auto GetFirstChild() const noexcept -> const NodeHandle&
  {
    return first_child_;
  }
  [[nodiscard]] auto GetNextSibling() const noexcept -> const NodeHandle&
  {
    return next_sibling_;
  }
  [[nodiscard]] auto GetPrevSibling() const noexcept -> const NodeHandle&
  {
    return prev_sibling_;
  }
  auto SetParent(const NodeHandle& parent) noexcept -> void
  {
    parent_ = parent;
  }
  auto SetFirstChild(const NodeHandle& child) noexcept -> void
  {
    first_child_ = child;
  }
  auto SetNextSibling(const NodeHandle& sibling) noexcept -> void
  {
    next_sibling_ = sibling;
  }
  auto SetPrevSibling(const NodeHandle& sibling) noexcept -> void
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
  NodeHandle parent_;
  NodeHandle first_child_;
  NodeHandle next_sibling_;
  NodeHandle prev_sibling_;
};

} // namespace oxygen::scene::detail
