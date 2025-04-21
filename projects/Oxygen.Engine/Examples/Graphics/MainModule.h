//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen {
namespace platform {
    class Platform;
    class Window;
} // namespace platform
namespace graphics {
    class Graphics;
} // namespace graphics
namespace co {
    class Nursery;
} // namespace co
} // namespace oxygen

#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Platform/Platform.h>

namespace oxygen::examples {

class MainModule final {
public:
    MainModule(std::shared_ptr<oxygen::Platform> platform,
        std::weak_ptr<oxygen::Graphics> gfx_weak);
    ~MainModule() = default;

    OXYGEN_MAKE_NON_COPYABLE(MainModule);
    OXYGEN_MAKE_NON_MOVABLE(MainModule);

    auto StartAsync(co::TaskStarted<> started = {}) -> co::Co<>
    {
        return OpenNursery(nursery_, std::move(started));
    }

    void Run();

private:
    void SetupCommandQueues();
    void SetupMainWindow();
    void SetupSurface();

    std::shared_ptr<oxygen::Platform> platform_;
    std::weak_ptr<oxygen::Graphics> gfx_weak_;
    std::weak_ptr<oxygen::platform::Window> window_weak_;
    std::unique_ptr<oxygen::graphics::Surface> surface_ {};

    co::Nursery* nursery_ { nullptr };
};

} // namespace oxygen::examples
