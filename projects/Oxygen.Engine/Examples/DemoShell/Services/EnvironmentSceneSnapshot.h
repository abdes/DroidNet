//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen::examples {

//! Restores scene-authored state changed by DemoShell environment controls.
/*!
 Capture before applying a custom profile or adding preview lighting. Only the
 sky, atmosphere, fog, directional lights, and local fog components are saved.
 Other environment systems retain their current values. Scene identity is held
 weakly; restoring another scene or an expired snapshot has no effect.

 Restoration preserves node topology. Removed original nodes are not recreated,
 and local fog components added since capture are removed without deleting their
 owning nodes. The caller must remove its preview sun before restoring.
*/
class EnvironmentSceneSnapshot final {
public:
  EnvironmentSceneSnapshot();
  ~EnvironmentSceneSnapshot();

  OXYGEN_MAKE_NON_COPYABLE(EnvironmentSceneSnapshot)
  OXYGEN_MAKE_NON_MOVABLE(EnvironmentSceneSnapshot)

  //! Captures a scene already placed in shared runtime ownership.
  auto Capture(scene::Scene& scene) -> void;
  auto Restore(scene::Scene& scene) const -> void;
  auto Reset() -> void;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace oxygen::examples
