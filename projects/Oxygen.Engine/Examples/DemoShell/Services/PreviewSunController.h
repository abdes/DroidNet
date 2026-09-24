//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen::examples {

enum class PreviewSunStatus {
  kDisabled,
  kBlockedByAuthoredSun,
  kSourceUnavailable,
  kReusedDirectional,
  kInjected,
};

//! Read-only sources for preview availability and its prospective light.
struct PreviewSunSources {
  //! The first authored sun or explicit atmosphere role, even if inactive.
  scene::SceneNode authored_sun;
  //! The borrowed source or visible-first directional; invalid for injection.
  scene::SceneNode candidate;
};

//! Provides an opt-in preview sun when the scene has no authored sun or
//! explicit Primary/Secondary atmosphere-light role.
//!
//! Reuses an existing directional light or creates one when none exists.
//! Only a reused light's affects-world flag and atmosphere role are
//! temporarily changed. Intensity, transforms, other lights, and environment
//! systems are untouched. Call Update with false before releasing a live scene
//! if its authored state must be restored; Reset is for scene lifetime
//! transitions.
class PreviewSunController final {
public:
  PreviewSunController() = default;
  ~PreviewSunController() = default;

  OXYGEN_MAKE_NON_COPYABLE(PreviewSunController)
  OXYGEN_MAKE_NON_MOVABLE(PreviewSunController)

  //! An explicit source name must identify exactly one directional light.
  auto Update(scene::Scene& scene, bool enabled,
    std::string_view preferred_node_name = {}) -> void;

  //! Relinquishes preview roles before a newly authored sun is resolved.
  //! Safe during observer dispatch: never changes topology or syncs observers.
  //! A created node stays inactive until the next Update can remove it.
  auto YieldToAuthoredSun(scene::Scene& scene) -> void;

  //! Releases handles without accessing the previously bound scene.
  auto Reset() noexcept -> void;

  [[nodiscard]] auto GetSun() const -> scene::SceneNode
  {
    return yielded_ ? scene::SceneNode {} : sun_;
  }
  [[nodiscard]] auto IsInjected() const -> bool { return injected_; }
  [[nodiscard]] auto InspectSources(scene::Scene& scene) const
    -> PreviewSunSources;
  [[nodiscard]] auto CanEnable(scene::Scene& scene) const -> bool;
  [[nodiscard]] auto GetStatus() const -> PreviewSunStatus { return status_; }

private:
  struct AuthoredDirectionalState {
    bool affects_world;
    scene::AtmosphereLightSlot atmosphere_slot;
  };

  auto ReleaseSun(scene::Scene& scene, bool synchronize = true) -> void;

  scene::Scene* bound_scene_ { nullptr };
  scene::SceneNode sun_;
  std::optional<AuthoredDirectionalState> authored_state_;
  std::string preferred_node_name_;
  bool injected_ { false };
  bool yielded_ { false };
  PreviewSunStatus status_ { PreviewSunStatus::kDisabled };
};

} // namespace oxygen::examples
