//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! Flags for clearing buffers in a render pass.
enum class ClearFlags : uint8_t {
  kNone = 0,

  kColor = OXYGEN_FLAG(0),
  kDepth = OXYGEN_FLAG(1),
  kStencil = OXYGEN_FLAG(2),

  //! Sentinel maximum value for ClearFlags.
  kMaxClearFlags = OXYGEN_FLAG(3),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(ClearFlags)

//! String representation of enum values in `DescriptorVisibility`.
OXGN_GFX_API auto to_string(ClearFlags value) -> std::string;

} // namespace oxygen::graphics
