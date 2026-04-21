//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include <glm/vec3.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {
class SceneNodeImpl;
class DirectionalLight;

class DirectionalLightContractError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class ResolvedDirectionalLightView final {
public:
  OXGN_SCN_API ResolvedDirectionalLightView(NodeHandle node_handle,
    const SceneNodeImpl& node, const DirectionalLight& light,
    const glm::vec3& emitted_ray_direction_ws,
    const glm::vec3& direction_to_light_ws,
    std::uint32_t traversal_index) noexcept;

  [[nodiscard]] auto NodeHandle() const noexcept -> scene::NodeHandle
  {
    return node_handle_;
  }

  [[nodiscard]] auto Node() const noexcept -> const SceneNodeImpl&
  {
    return node_.get();
  }

  [[nodiscard]] auto Light() const noexcept -> const DirectionalLight&
  {
    return light_.get();
  }

  [[nodiscard]] auto EmittedRayDirectionWs() const noexcept -> glm::vec3
  {
    return emitted_ray_direction_ws_;
  }

  [[nodiscard]] auto DirectionToLightWs() const noexcept -> glm::vec3
  {
    return direction_to_light_ws_;
  }

  [[nodiscard]] auto TraversalIndex() const noexcept -> std::uint32_t
  {
    return traversal_index_;
  }

private:
  scene::NodeHandle node_handle_ {};
  std::reference_wrapper<const SceneNodeImpl> node_;
  std::reference_wrapper<const DirectionalLight> light_;
  glm::vec3 emitted_ray_direction_ws_ { 0.0F, -1.0F, 0.0F };
  glm::vec3 direction_to_light_ws_ { 0.0F, 1.0F, 0.0F };
  std::uint32_t traversal_index_ { 0U };
};

class DirectionalLightResolver final : public ISceneObserver {
public:
  OXGN_SCN_API DirectionalLightResolver() = default;
  OXGN_SCN_API ~DirectionalLightResolver() override;

  OXYGEN_MAKE_NON_COPYABLE(DirectionalLightResolver)
  OXYGEN_MAKE_NON_MOVABLE(DirectionalLightResolver)

  OXGN_SCN_API auto Bind(observer_ptr<Scene> scene) -> void;
  OXGN_SCN_API auto Unbind() noexcept -> void;

  OXGN_SCN_NDAPI auto IsValid() const -> bool;
  OXGN_SCN_API auto Validate() const -> void;

  OXGN_SCN_NDAPI auto ResolvePrimarySun() const
    -> std::optional<ResolvedDirectionalLightView>;
  OXGN_SCN_NDAPI auto ResolveSecondarySun() const
    -> std::optional<ResolvedDirectionalLightView>;
  OXGN_SCN_NDAPI auto ResolveMoon() const
    -> std::optional<ResolvedDirectionalLightView>;
  OXGN_SCN_NDAPI auto ResolveDirectionalLights() const
    -> std::span<const ResolvedDirectionalLightView>;

  OXGN_SCN_API auto OnLightChanged(const NodeHandle& node_handle) noexcept
    -> void override;
  OXGN_SCN_API auto OnTransformChanged(const NodeHandle& node_handle) noexcept
    -> void override;
  OXGN_SCN_API auto OnNodeDestroyed(const NodeHandle& node_handle) noexcept
    -> void override;

private:
  auto RebuildIfDirty() const -> void;
  auto MarkDirty() noexcept -> void;
  auto ResolvePrimaryEnvironmentLight() const
    -> std::optional<ResolvedDirectionalLightView>;
  auto ResolveSecondaryEnvironmentLight() const
    -> std::optional<ResolvedDirectionalLightView>;
  auto ValidationErrorMessage() const -> std::optional<std::string>;

  observer_ptr<Scene> scene_ { nullptr };
  mutable bool dirty_ { true };
  mutable bool valid_ { true };
  mutable std::optional<std::string> validation_error_ {};
  mutable std::vector<ResolvedDirectionalLightView> directional_lights_ {};
  mutable std::optional<ResolvedDirectionalLightView> primary_sun_light_ {};
  mutable std::optional<ResolvedDirectionalLightView> primary_environment_light_ {};
  mutable std::optional<ResolvedDirectionalLightView> secondary_environment_light_ {};
};

} // namespace oxygen::scene
