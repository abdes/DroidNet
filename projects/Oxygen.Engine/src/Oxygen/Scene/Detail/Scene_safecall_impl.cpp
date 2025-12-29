//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ResourceTable.h>
#include <Oxygen/Scene/Detail/Scene_safecall_impl.h>
#include <Oxygen/Scene/Scene.h>

// =============================================================================
// Scene Validator Implementations
// =============================================================================

using oxygen::scene::Scene;

void Scene::LogSafeCallError([[maybe_unused]] const char* reason) noexcept
{
  try {
    DLOG_F(ERROR, "Graph operation failed: {}", reason);
  } catch (...) {
    // If logging fails, we can do nothing about it
    (void)0;
  }
}
static_assert(oxygen::HasLogSafeCallError<Scene>);

auto Scene::NodeIsValidAndMine(SceneNode& node) const
  -> NodeIsValidAndInSceneValidator
{
  return NodeIsValidAndInSceneValidator(this, node);
}

auto Scene::NodeIsValidAndInScene(SceneNode& node) const
  -> NodeIsValidAndInSceneValidator
{
  const Scene* scene { nullptr };
  if (!node.scene_weak_.expired()) [[likely]] {
    scene = node.scene_weak_.lock().get();
  }
  return NodeIsValidAndInSceneValidator(scene, node);
}

auto Scene::LeafNodeCanBeDestroyed(SceneNode& node) const
  -> LeafNodeCanBeDestroyedValidator
{
  return LeafNodeCanBeDestroyedValidator(this, node);
}

/*!
 Validates scene existence. The scene must not be null for any validator
 operations to proceed. A null scene indicates a critical programming error.

 @return Always returns true (aborts on validation failure)
*/
auto Scene::BaseNodeValidator::EnsureScene() -> bool
{
  if (GetScene() == nullptr) [[unlikely]] {
    // This is a programming logic error, and is fatal.
    ABORT_F(
      "scene for node({}) does not exit anymore", nostd::to_string(GetNode()));
  }
  result_.reset();
  return true;
}

/*!
 Validates node ownership. The target scene must own the node being validated.

 @return Always returns true (aborts on validation failure)
*/
auto Scene::BaseNodeValidator::EnsureSceneOwnsNode() -> bool
{
  DCHECK_F(GetScene() != nullptr, "call EnsureScene() before");

  if (!GetScene()->IsOwnerOf(GetNode())) [[unlikely]] {
    // This is a programming logic error, and is fatal.
    ABORT_F("node({}) does not belong to scene `{}`",
      nostd::to_string(GetNode()), GetScene()->GetName());
  }
  result_.reset();
  return true;
}

/*!
 Validates leaf node requirement. The node must have no children for operations
 that only work on leaf nodes. Nodes with children require hierarchy-aware
 operations instead.

 @return Always returns true (aborts on validation failure)
*/
auto Scene::BaseNodeValidator::EnsureNodeHasNoChildren() -> bool
{
  DCHECK_NOTNULL_F(GetScene(), "call EnsureScene() before");

  if (GetNode().HasChildren()) [[unlikely]] {
    // This is a programming logic error, and is fatal.
    ABORT_F("node({}) has children; use hierarchy API methods on it",
      nostd::to_string(GetNode()), GetScene()->GetName());
  }
  result_.reset();
  return true;
}

/*!
 Validates node handle validity. The node handle must be in a valid state
 to proceed with operations. Invalid handles indicate stale or corrupted
 node references.

 @return true if valid, false if invalid (stores error message)
*/
auto Scene::BaseNodeValidator::CheckNodeIsValid() -> bool
{
  if (!GetNode().IsValid()) [[unlikely]] {
    result_ = fmt::format("node({}) is invalid", nostd::to_string(GetNode()));
    return false;
  }
  result_.reset();
  return true;
}

/*!
 Validates node existence in scene table. The node must still exist in the
 scene's internal storage. Missing nodes are lazily invalidated to prevent
 future use of stale references.

 @param state SafeCallState to populate with node implementation pointer

 @return true if node exists and state populated, false if node missing
 (invalidates node and stores error message)
*/
auto Scene::BaseNodeValidator::PopulateStateWithNodeImpl(SafeCallState& state)
  -> bool
{
  DCHECK_NOTNULL_F(GetScene(), "call EnsureScene() before");

  // Then check if the node is still in the scene node table, and retrieve
  // its implementation object.
  try {
    auto& impl = GetScene()->nodes_->ItemAt(GetNode().GetHandle());
    state.node_impl = &impl;
    result_.reset();
    return true;
  } catch (const std::exception&) {
    // Invalidate the node (lazy invalidation)
    state.node->Invalidate();
    result_
      = fmt::format("node({}) is no longer in scene `{}` -> lazily invalidated",
        nostd::to_string(GetNode().GetHandle()), GetScene()->GetName());
    return false;
  }
}
