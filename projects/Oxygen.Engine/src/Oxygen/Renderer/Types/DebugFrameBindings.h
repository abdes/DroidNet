//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::engine {

//! Bindless debug-system routing payload for a single view.
struct alignas(16) DebugFrameBindings {
  ShaderVisibleIndex line_buffer_srv_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex line_buffer_uav_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex counter_buffer_uav_slot { kInvalidShaderVisibleIndex };
  std::array<std::uint32_t, 1> _pad_to_16 {};
};

static_assert(sizeof(DebugFrameBindings) == 16);
static_assert(alignof(DebugFrameBindings) == 16);

} // namespace oxygen::engine
