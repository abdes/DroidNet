//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! The intended use of the command queue.
/*!
 QueueRole expresses the intended use of a queue. The backend implementation
 is responsible for mapping these roles to API-specific queue types or families.
 For example, in D3D12, kPresent maps to a graphics queue.
*/
enum class QueueRole : uint8_t {
  kFirst = 0,

  kGraphics = kFirst, //!< Graphics command queue.
  kCompute = 1, //!< Compute command queue.
  kTransfer = 2, //!< Copy command queue.
  kPresent = 3, //!< Presentation command queue.

  kMax = 4 // Must be last
};

//! String representation of enum values in `QueueRole`.
OXGN_GFX_API auto to_string(QueueRole value) -> const char*;

} // namespace oxygen::graphics
