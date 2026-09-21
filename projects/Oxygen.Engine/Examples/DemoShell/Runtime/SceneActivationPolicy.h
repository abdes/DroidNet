//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

namespace oxygen::examples {

//! Authority for camera, environment and exposure values when a scene
//! activates.
enum class SceneActivationPolicy : std::uint8_t {
  kRestorePreferences,
  kExperimentOwned,
};

[[nodiscard]] inline auto to_string(const SceneActivationPolicy policy) noexcept
  -> std::string_view
{
  switch (policy) {
  case SceneActivationPolicy::kRestorePreferences:
    return "Restore Preferences";
  case SceneActivationPolicy::kExperimentOwned:
    return "Experiment Owned";
  }
  return "__NotSupported__";
}

} // namespace oxygen::examples
