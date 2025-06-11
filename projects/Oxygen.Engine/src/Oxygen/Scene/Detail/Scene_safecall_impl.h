//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Scene.h>

// =============================================================================
// Scene Validator Implementations
// =============================================================================

using oxygen::scene::Scene;

/*!
 Core implementation of the SafeCall mechanism for Scene operations.

 This template method provides a robust execution framework for Scene operations
 that require validation before execution. It integrates with the
 oxygen::SafeCall infrastructure to ensure operations are performed safely with
 proper error handling and state management.

 @tparam Self The type of the Scene instance (const Scene* or Scene*)
 @tparam Validator A callable that validates preconditions
 @tparam Func A callable that performs the actual operation

 @param self Pointer to the Scene instance
 @param validator Validation function that checks preconditions and populates
 state. Must return std::nullopt on success or error message on failure
 @param func Operation function that performs the actual work using the
 validated state. Executed only if validation succeeds

 @return The result of the operation function, or a default-constructed value if
 validation fails or an exception occurs

 @note This method is exception-safe and will catch and log any exceptions
 thrown by the operation function, returning a default-constructed result
 instead.
*/
template <typename Self, typename Validator, typename Func>
auto Scene::SafeCallImpl(
  Self* self, Validator validator, Func&& func) const noexcept
{
  SafeCallState state;
  auto result = oxygen::SafeCall(
    *self,
    [&](auto&& /*self_ref*/) -> std::optional<std::string> {
      // Invoke the validator to populate the state
      return validator(state);
    },
    [func = std::forward<Func>(func), &state](
      auto&& /*self_ref*/) mutable noexcept {
      try {
        return func(state);
      } catch (const std::exception& ex) {
        DLOG_F(ERROR, "scene operation failed due to exception: {}", ex.what());
        return decltype(func(state)) {}; // Return default-constructed type
      } catch (...) {
        DLOG_F(ERROR, "scene operation failed due to unknown error");
        return decltype(func(state)) {}; // Return default-constructed type
      }
    });

  // Extract the actual value from the oxygen::SafeCall result. This would
  // work with operations that return std::options<T> or bool.
  if (result.has_value()) {
    return result.value();
  }
  return decltype(func(state)) {}; // Return default-constructed type
}

template <typename Validator, typename Func>
auto Scene::SafeCall(Validator&& validator, Func&& func) const noexcept
{
  return SafeCallImpl(
    this, std::forward<Validator>(validator), std::forward<Func>(func));
}

template <typename Validator, typename Func>
auto Scene::SafeCall(Validator&& validator, Func&& func) noexcept
{
  return SafeCallImpl(
    this, std::forward<Validator>(validator), std::forward<Func>(func));
}

/*!
 Base class for Scene node validation logic in SafeCall operations.

 This abstract base class provides common functionality for validating SceneNode
 operations before they are executed. It encapsulates the target scene and node,
 provides protected access methods for derived validators, and manages error
 state accumulation.

 Derived classes should implement the validation logic specific to their
 operation type (e.g., node creation, destruction, re-parenting) by overriding
 the call operator and using the provided validation helper methods.

 @note This class is designed to work with the SafeCall infrastructure and
 maintains error state that can be retrieved after validation fails.
*/
class Scene::BaseNodeValidator {
public:
  BaseNodeValidator(
    const Scene* target_scene, const SceneNode& target_node) noexcept
    : scene_(target_scene)
    , node_(&target_node)
  {
  }

protected:
  //! Returns the target scene pointer for validation operations.
  [[nodiscard]] auto GetScene() const noexcept -> const Scene*
  {
    return scene_;
  }

  //! Returns the target node reference for validation operations.
  [[nodiscard]] auto GetNode() const noexcept -> const SceneNode&
  {
    return *node_;
  }

  //! Returns and moves the current validation error result.
  [[nodiscard]] auto GetResult() noexcept { return std::move(result_); }

  //! Validates that the target scene exists and is not null.
  auto EnsureScene() -> bool;

  //! Validates that the target scene owns the target node.
  auto EnsureSceneOwnsNode() -> bool;

  //! Validates that the target node has no child nodes.
  auto EnsureNodeHasNoChildren() -> bool;

  //! Validates that the target node is in a valid state.
  auto CheckNodeIsValid() -> bool;

  //! Populates the SafeCallState with the node implementation pointer.
  auto PopulateStateWithNodeImpl(SafeCallState& state) -> bool;

private:
  std::optional<std::string> result_ {};
  const Scene* scene_;
  const SceneNode* node_;
};

//! A validator that checks if a SceneNode is valid and belongs to the \p scene.
class Scene::NodeIsValidAndInSceneValidator : public BaseNodeValidator {
public:
  explicit NodeIsValidAndInSceneValidator(
    const Scene* target_scene, const SceneNode& target_node) noexcept
    : BaseNodeValidator(target_scene, target_node)
  {
  }

  auto operator()(SafeCallState& state) -> std::optional<std::string>
  {
    state.node = const_cast<SceneNode*>(&GetNode());
    if (CheckNodeIsValid() && EnsureScene() && EnsureSceneOwnsNode()
      && PopulateStateWithNodeImpl(state)) [[likely]] {
      // All validations passed
      return std::nullopt;
    }
    return GetResult();
  }
};

//! A validator that checks if a SceneNode is valid and belongs to the \p scene.
class Scene::LeafNodeCanBeDestroyedValidator : public BaseNodeValidator {
public:
  explicit LeafNodeCanBeDestroyedValidator(
    const Scene* target_scene, const SceneNode& target_node) noexcept
    : BaseNodeValidator(target_scene, target_node)
  {
  }

  auto operator()(SafeCallState& state) -> std::optional<std::string>
  {
    state.node = const_cast<SceneNode*>(&GetNode());
    if (EnsureScene() && EnsureSceneOwnsNode()
      && PopulateStateWithNodeImpl(state) && EnsureNodeHasNoChildren())
      [[likely]] {
      // All validations passed
      return std::nullopt;
    }
    return GetResult();
  }
};
