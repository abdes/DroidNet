//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/StaticVector.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/DepthPrePass.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/RenderItem.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen {
namespace platform {
    class Platform;
    class Window;
} // namespace platform
class Graphics;
namespace graphics {
    class CommandRecorder;
    class RenderTarget;
    class RenderController;
    class Surface;
    class Framebuffer;
    class Buffer;
} // namespace graphics
namespace co {
    class Nursery;
} // namespace co
} // namespace oxygen

namespace oxygen::examples {

class MainModule final {
public:
    MainModule(std::shared_ptr<oxygen::Platform> platform,
        std::weak_ptr<oxygen::Graphics> gfx_weak);
    ~MainModule();

    OXYGEN_MAKE_NON_COPYABLE(MainModule)
    OXYGEN_MAKE_NON_MOVABLE(MainModule)

    auto StartAsync(co::TaskStarted<> started = {}) -> co::Co<>
    {
        return OpenNursery(nursery_, std::move(started));
    }

    void Run();

private:
    void SetupCommandQueues() const;
    void SetupMainWindow();
    void SetupSurface();
    void SetupRenderer();
    void SetupFramebuffers();
    void SetupRenderPasses();

    auto RenderScene() -> co::Co<>;

    std::shared_ptr<Platform> platform_;
    std::weak_ptr<Graphics> gfx_weak_;
    std::weak_ptr<platform::Window> window_weak_;
    std::shared_ptr<graphics::Surface> surface_;
    std::shared_ptr<graphics::RenderController> renderer_;
    StaticVector<std::shared_ptr<graphics::Framebuffer>, kFrameBufferCount> framebuffers_ {};

    std::shared_ptr<graphics::Buffer> constant_buffer_;
    graphics::NativeObject index_mapping_cbv_ {};

    co::Nursery* nursery_ { nullptr };
    float rotation_angle_ { 0.0f };

    std::vector<oxygen::graphics::RenderItem> render_items_;
    std::vector<const oxygen::graphics::RenderItem*> draw_list_;
    std::shared_ptr<oxygen::graphics::DepthPrePassConfig> depth_pre_pass_config_;
    std::unique_ptr<oxygen::graphics::DepthPrePass> depth_pre_pass_;
};

} // namespace oxygen::examples
