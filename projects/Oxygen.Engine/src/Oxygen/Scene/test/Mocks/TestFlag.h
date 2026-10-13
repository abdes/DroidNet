//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Scene/SceneFlags.h>

using oxygen::scene::AtomicSceneFlags;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneFlagEnum;
using oxygen::scene::SceneFlags;

namespace oxygen::scene::testing {

//! Test enum for SceneFlags.
//! This enum defines a set of flags used for testing the SceneFlags class.
enum class TestFlag : uint8_t {
  kVisible, //!< Represents visibility status.
  kLocked, //!< Represents locked status.
  kSelected, //!< Represents selected status.
  kCount //!< Represents the total number of flags.
};
static_assert(SceneFlagEnum<TestFlag>);
[[maybe_unused]] auto constexpr to_string(const TestFlag& value) noexcept
  -> const char*
{
  switch (value) {
  case TestFlag::kVisible:
    return "Visible";
  case TestFlag::kLocked:
    return "Locked";
  case TestFlag::kSelected:
    return "Selected";

  case TestFlag::kCount:
    break; // Sentinel value, not used
  }
  return "__NotSupported__";
}

} // namespace oxygen::scene::testing
