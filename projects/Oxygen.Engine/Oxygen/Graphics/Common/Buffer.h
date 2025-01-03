//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include "Oxygen/Base/Macros.h"

namespace oxygen::renderer {

/**
 * Buffer's view for binding
 */
struct BufferView {
  uint32_t firstElement = 0;
  uint32_t numElements = UINT32_MAX;
};

class Buffer
{
 public:
  Buffer() = default;
  virtual ~Buffer() = default;

  OXYGEN_MAKE_NON_COPYABLE(Buffer);
  OXYGEN_MAKE_NON_MOVEABLE(Buffer);

  virtual void Bind() = 0;
  virtual void* Map() = 0;
  virtual void Unmap() = 0;
};

} // namespace oxygen::renderer
