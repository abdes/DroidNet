//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Types/RenderTask.h>

namespace oxygen::graphics {

class RenderTarget;

namespace detail {
    class RenderThread : public oxygen::Component {
        OXYGEN_COMPONENT(RenderThread)
        OXYGEN_COMPONENT_REQUIRES(oxygen::ObjectMetaData)
    public:
        using BeginFrameFn = std::function<void()>;
        using EndFrameFn = std::function<void()>;

        RenderThread(uint32_t frames_in_flight = kFrameBufferCount - 1,
            BeginFrameFn begin_frame_fn = nullptr,
            EndFrameFn end_frame_fn = nullptr);

        ~RenderThread() override;

        OXYGEN_MAKE_NON_COPYABLE(RenderThread); //< Non-copyable.
        OXYGEN_DEFAULT_MOVABLE(RenderThread); //< Non-moveable.

        void Submit(FrameRenderTask task);

        void Stop();

    protected:
        void UpdateDependencies(const oxygen::Composition& composition) override;

    private:
        void Start();

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace detail

} // namespace oxygen::graphics
