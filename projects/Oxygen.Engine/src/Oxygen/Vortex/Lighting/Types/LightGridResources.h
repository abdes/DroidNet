//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridBuildStatus.h>

namespace oxygen::graphics {
class Buffer;
}
namespace oxygen::vortex {

struct CompletedLightGridBuild {
  frame::SequenceNumber sequence { 0U };
  LightGridBuildStatus status;
};

//! Retained current-view products for explicit diagnostics/readback.
struct LightGridResources {
  std::shared_ptr<const graphics::Buffer> status;
  std::shared_ptr<const graphics::Buffer> ranges;
  std::shared_ptr<const graphics::Buffer> indices;
};

} // namespace oxygen::vortex
