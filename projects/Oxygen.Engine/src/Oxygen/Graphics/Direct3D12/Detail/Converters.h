//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Graphics/Common/Types/ResourceStates.h>

namespace oxygen::graphics::d3d12::detail {

auto ConvertResourceStates(ResourceStates states) -> D3D12_RESOURCE_STATES;

} // namespace oxygen::graphics::d3d12::detail
