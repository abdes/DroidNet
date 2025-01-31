//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Signals.h"
#include "Oxygen/Graphics/Common/PerFrameResourceManager.h"

namespace oxygen::graphics {

//! Mixin class that provides deferred resource release functionality.
/*!
 \tparam Base The base class to mix-in the deferred resource release

 \note This mixin requires the `MixinRendererEvents` mixin to be also mixed-in.
 When the `GetPerFrameResourceManager()` is called for the first time, it
 hooks itself with the frame and renderer events.

 When the rendering for a frame index is starting, it releases all resources
 that were deferred for release from the previous render of that frame index.

 When the renderer is shutting down, it releases all deferred resources from
 all frames.
 */
// ReSharper disable once CppClassCanBeFinal (mixin should never be made final)
template <typename Base>
class MixinDeferredRelease : public Base {
public:
    //! Forwarding constructor, required to pass the arguments to the other
    //! mixin classes in the mixin chain.
    template <typename... Args>
    constexpr explicit MixinDeferredRelease(Args&&... args)
        : Base(std::forward<Args>(args)...)
    {
    }

    virtual ~MixinDeferredRelease() = default;

    OXYGEN_DEFAULT_COPYABLE(MixinDeferredRelease);
    OXYGEN_DEFAULT_MOVABLE(MixinDeferredRelease);

    //! Returns the per-frame resource manager, primarily used by the helper
    //! function `DeferredObjectRelease()`.
    /*!
     \note This method is used to lazily initialize the per-frame resource manager.
    */
    auto GetPerFrameResourceManager() noexcept -> PerFrameResourceManager&
    {
        static bool is_initialized { false };
        if (!is_initialized) {
            InitializeDeferredRelease();
            is_initialized = true;
        }

        return resource_manager_;
    }

private:
    void InitializeDeferredRelease()
    {
        LOG_SCOPE_FUNCTION(INFO);
        // Hook into the frame rendering lifecycle events.
        begin_frame_ = this->self().OnBeginFrameRender().connect(
            [this](const uint32_t frame_index) {
                resource_manager_.OnBeginFrame(frame_index);
            });
        // Hook into the renderer lifecycle events.
        renderer_shutdown_ = this->self().OnRendererShutdown().connect(
            [this]() {
                ShutdownDeferredRelease();
            });
    }

    void ShutdownDeferredRelease()
    {
        LOG_SCOPE_FUNCTION(INFO);

        // Execute all pending deferred releases.
        resource_manager_.ReleaseAllDeferredResources();

        // Disconnect from the frame rendering and the renderer lifecycle events.
        begin_frame_.disconnect();
        renderer_shutdown_.disconnect();
    }

    PerFrameResourceManager resource_manager_;

    sigslot::connection begin_frame_;
    sigslot::connection renderer_shutdown_;
};

} // namespace oxygen::graphics::d3d12::detail
