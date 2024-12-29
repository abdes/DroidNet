//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/base/Macros.h"
#include "Oxygen/Renderers/Common/Types.h"
#include "oxygen/Renderers/Direct3d12/ResourceState.h"

namespace oxygen::renderer::d3d12 {

  class D3DResource
  {
  public:
    virtual ~D3DResource() noexcept = default;

    OXYGEN_MAKE_NON_COPYABLE(D3DResource);
    OXYGEN_DEFAULT_MOVABLE(D3DResource);

    [[nodiscard]] virtual auto GetResource() const->ID3D12Resource* = 0;
    [[nodiscard]] virtual auto GetState() const -> ResourceState { return state_; }
    [[nodiscard]] virtual auto GetMode() const -> ResourceAccessMode { return mode_; }

  protected:
    D3DResource() = default;

    ResourceState state_{};
    ResourceAccessMode mode_{ ResourceAccessMode::kImmutable };
  };

}  // namespace oxygen::renderer::d3d12
