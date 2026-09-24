//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

namespace oxygen::graphics {
class Buffer;
}
namespace oxygen::vortex {

//! Retained current-view products for explicit diagnostics/readback.
struct LightGridResources {
  std::shared_ptr<const graphics::Buffer> status;
  std::shared_ptr<const graphics::Buffer> ranges;
  std::shared_ptr<const graphics::Buffer> indices;
};

} // namespace oxygen::vortex
