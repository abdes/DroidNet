//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Composition/Component.h>

namespace oxygen::composition::detail {

class ComponentPoolUntyped {
public:
  virtual ~ComponentPoolUntyped() = default;

  virtual auto GetUntyped(ResourceHandle handle) const noexcept
    -> const Component* = 0;
  virtual auto GetUntyped(ResourceHandle handle) noexcept -> Component* = 0;
  virtual auto Allocate(Component&& src) -> ResourceHandle = 0;
  virtual auto Deallocate(ResourceHandle handle) noexcept -> size_t = 0;
};

} // namespace oxygen::composition::detail
