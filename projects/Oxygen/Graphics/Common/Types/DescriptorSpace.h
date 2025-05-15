//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace Oxygen::Graphics {

/*!
 Defines memory locations where descriptors can reside.

 This is primarily an implementation detail for the descriptor system, used to
 optimize descriptor updates and placement. In D3D12, this maps to whether a
 descriptor resides in a shader-visible heap or a non-shader-visible heap. In
 Vulkan, this maps to host-visible vs device-local descriptor pools.
*/
enum class DescriptorSpace {
  ShaderVisible,   //!< GPU-accessible.
  NonShaderVisible //!< CPU-only.
};

}  // namespace Oxygen::Graphics
