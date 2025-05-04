//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Types/Scissors.h>
#include <Oxygen/Graphics/Common/Types/ViewPort.h>

namespace oxygen::graphics {

class RenderTarget {
public:
    RenderTarget() = default;
    virtual ~RenderTarget() = default;

    OXYGEN_MAKE_NON_COPYABLE(RenderTarget);
    OXYGEN_DEFAULT_MOVABLE(RenderTarget);

    [[nodiscard]] virtual auto GetViewPort() const -> const ViewPort& = 0;
    [[nodiscard]] virtual auto GetScissors() const -> const Scissors& = 0;
};

} // namespace oxygen
