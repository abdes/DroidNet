//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Types/RenderTask.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::graphics::detail {

class RenderThread : public oxygen::Component {
    OXYGEN_COMPONENT(RenderThread)
public:
    RenderThread(uint32_t frames_in_flight = kFrameBufferCount - 1);
    ~RenderThread() override;

    OXYGEN_MAKE_NON_COPYABLE(RenderThread); //< Non-copyable.
    OXYGEN_DEFAULT_MOVABLE(RenderThread); //< Non-moveable.

    void Submit(FrameRenderTask task);

    void Stop();

private:
    void Start();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::graphics::detail
