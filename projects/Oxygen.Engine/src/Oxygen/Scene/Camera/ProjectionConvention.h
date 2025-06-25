//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene::camera {

//! Supported projection conventions for graphics APIs
enum class ProjectionConvention {
  kD3D12, //!< Right-handed, z in [0, 1], Y+ up (Direct3D 12)
  kVulkan //!< Right-handed, z in [0, 1], Y+ down (Vulkan)
};

//! Converts a ProjectionConvention enum to a string representation.
inline auto to_string(const ProjectionConvention& value) -> const char*
{
  switch (value) {
  case ProjectionConvention::kD3D12:
    return "D3D12";
  case ProjectionConvention::kVulkan:
    return "Vulkan";
  }

  return "__NotSupported__";
}

} // namespace oxygen::scene
