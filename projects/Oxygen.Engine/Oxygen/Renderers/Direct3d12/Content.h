//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "Oxygen/Base/Types.h"

namespace oxygen::content {

  auto LoadEngineShaders(std::unique_ptr<uint8_t[]>& shaders, uint64_t& size) -> bool;

}  // namespace oxygen::renderer::d3d12
