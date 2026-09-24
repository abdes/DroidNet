//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>

#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen {
class Component;
}

namespace oxygen::scene {

//! Owned diagnostic for a rejected whole-light edit; accepted state is unchanged.
struct LightValidationError {
  std::string field;
  std::string message;
  NodeHandle conflicting_node;
};

//! Validate detached authored values before attaching or replacing a scene light.
//! SceneNode additionally checks atmosphere-slot ownership across stored lights.
OXGN_SCN_NDAPI auto ValidateLight(const Component& light)
  -> std::optional<LightValidationError>;

} // namespace oxygen::scene
