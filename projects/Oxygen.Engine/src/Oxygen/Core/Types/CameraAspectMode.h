//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen {

//! Authored perspective-camera framing policy.
enum class CameraAspectMode : std::uint8_t {
  kAuto = 0,
  kFixed = 1,
};

inline constexpr float kDefaultCameraAspectRatio = 16.0F / 9.0F;

} // namespace oxygen
