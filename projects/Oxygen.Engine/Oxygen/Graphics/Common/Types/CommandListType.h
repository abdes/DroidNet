//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Types.h"
#include "Oxygen/api_export.h"

namespace oxygen::graphics {

//! Types of command queues.
enum class CommandListType : int8_t {
  kGraphics = 0, //!< Graphics command queue.
  kCompute = 1, //!< Compute command queue.
  kCopy = 2, //!< Copy command queue.

  kNone = -1 //!< Invalid command queue.
};

//! String representation of enum values in `CommandListType`.
OXYGEN_API auto to_string(CommandListType value) -> const char*;

} // namespace oxygen::graphics
