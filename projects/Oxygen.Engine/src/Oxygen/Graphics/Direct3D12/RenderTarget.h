//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/RenderTarget.h>
#include <Oxygen/Graphics/Direct3d12/Detail/DescriptorHeap.h>

namespace oxygen::graphics::d3d12 {

class RenderTarget : public graphics::RenderTarget {
    using Base = graphics::RenderTarget;

public:
    RenderTarget() = default;
    ~RenderTarget() noexcept override = default;

    OXYGEN_DEFAULT_COPYABLE(RenderTarget);
    OXYGEN_DEFAULT_MOVABLE(RenderTarget);

    [[nodiscard]] virtual auto GetResource() const -> ID3D12Resource* = 0;
    [[nodiscard]] virtual auto Rtv() const -> const detail::DescriptorHandle& = 0;
};

} // namespace oxygen::graphics::d3d12
