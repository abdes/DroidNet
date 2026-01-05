//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene::environment {

//! Base component for scene-global environment systems.
/*!
 Environment systems live on a `SceneEnvironment` composition (not on nodes).
 They are authored data containers: they hold parameters, but do not own GPU
 resources.

 @note This component intentionally only provides an `enabled` toggle. Each
       derived system defines its own minimal parameter set.
*/
class EnvironmentSystem : public Component {
public:
  //! Constructs an enabled environment system.
  EnvironmentSystem() = default;

  //! Virtual destructor.
  ~EnvironmentSystem() override = default;

  OXYGEN_DEFAULT_COPYABLE(EnvironmentSystem)
  OXYGEN_DEFAULT_MOVABLE(EnvironmentSystem)

  //! Enables or disables this system.
  auto SetEnabled(const bool enabled) noexcept -> void { enabled_ = enabled; }

  //! Returns whether this system is enabled.
  OXGN_SCN_NDAPI auto IsEnabled() const noexcept -> bool { return enabled_; }

private:
  bool enabled_ = true;
};

} // namespace oxygen::scene::environment
