//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen {
namespace platform {
    class Platform;
    class Window;
} // namespace platform
class Graphics;
namespace graphics {
    class RenderTarget;
    class Renderer;
    class Surface;
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
    void SetupCommandQueues();
    void SetupMainWindow();
    void SetupSurface();
    void SetupRenderer();

    auto RenderScene() -> oxygen::co::Co<>;

    std::shared_ptr<oxygen::Platform> platform_;
    std::weak_ptr<oxygen::Graphics> gfx_weak_;
    std::weak_ptr<oxygen::platform::Window> window_weak_;
    std::shared_ptr<oxygen::graphics::Surface> surface_ {};
    std::shared_ptr<oxygen::graphics::Renderer> renderer_ {};

    co::Nursery* nursery_ { nullptr };
};

} // namespace oxygen::examples
