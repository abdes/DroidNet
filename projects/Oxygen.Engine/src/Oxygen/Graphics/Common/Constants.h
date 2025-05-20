//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen {

//! The number of frame buffers we manage
constexpr uint32_t kFrameBufferCount { 3 };

//! The maximum number of render targets that can be bound to a command list or
//! configured in a pipeline state.
constexpr uint32_t kMaxRenderTargets = 8;

//! Bindless layouts allow applications to attach a descriptor table to an
//! unbound resource array in the shader. The same table can be bound to
//! multiple register spaces on DX12, in order to access different types of
//! resources stored in the table through different arrays.
constexpr uint32_t kMaxRegisterSpacesToBindTable = 16;

} // namespace oxygen
