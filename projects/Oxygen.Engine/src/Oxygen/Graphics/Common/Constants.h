//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen {

//! The maximum number of render targets that can be bound to a command list or
//! configured in a pipeline state.
constexpr uint32_t kMaxRenderTargets = 8;

} // namespace oxygen
