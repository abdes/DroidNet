//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Scene/Types/SceneFlagEnum.h>

namespace oxygen::scene {

//! SceneNodeFlags are symbolic flags for node state and optimization hints.
enum class SceneNodeFlags : uint8_t {
  kVisible, //!< Node is visible for rendering
  kStatic, //!< Node transform won't change (optimization hint)
  kCastsShadows, //!< Node casts shadows
  kReceivesShadows, //!< Node receives shadows
  kRayCastingSelectable, //!< Node can be selected via ray casting
  kIgnoreParentTransform, //!< Ignore parent transform (use only local
                          //!< transform)

  kCount, //!< Sentinel value required for TernaryFlagEnum concept
};

auto to_string(const SceneNodeFlags& value) noexcept -> const char*;

static_assert(SceneFlagEnum<SceneNodeFlags>,
  "SceneNodeFlags must satisfy TernaryFlagEnum concept requirements");

} // namespace oxygen::scene
