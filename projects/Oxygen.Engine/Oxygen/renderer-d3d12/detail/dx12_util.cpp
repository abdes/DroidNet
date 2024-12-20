//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "dx12_utils.h"

#include <vector>

#include "oxygen/renderer/types.h"
#include "resources.h"

void oxygen::renderer::direct3d12::detail::DeferRelease(IUnknown* resource) noexcept
{
  auto& instance = DeferredRelease::Instance();
  try {
    instance.DeferRelease(resource);
  }
  catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to defer release of resource: {}", e.what());
    resource->Release();
  }
}
