//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <stdexcept>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/BackendModule.h>

namespace oxygen {

class Graphics;
struct GraphicsConfig;

namespace loader {
    //! Exception thrown when the loader is used from a module other than the
    //! main executable module, before it has been first initialized from the
    //! main executable module.
    class InvalidOperationError final : public std::logic_error {
        using std::logic_error::logic_error;
    };

    namespace detail {
        class PlatformServices;
    } // namespace detail
} // namespace loader

//! A singleton class that dynamically loads and unloads a graphics backend.
/*!
 This loader imposes the restriction (to ensure a single instance across the
 process) that it should be first initialized from the main executable module.
 Any attempt to access the single instance of the loader from another module
 before it has been initialized will result in a `InvalidOperationError`
 exception.

 For testability purposes, the loader can be constructed with a custom
 `PlatformServices` implementation. This allows to inject a mock implementation
 during testing. If no custom implementation is provided, the default platform
 services implementation will be used.
*/
class GraphicsBackendLoader {
public:
    //! Gets the singleton instance of the graphics backend loader.
    /*!
     \param platform_services An optional custom platform services
     implementation to use. When not provided, the default platform services
     implementation will be used.
    */
    static auto GetInstance(std::shared_ptr<loader::detail::PlatformServices> platform_services = nullptr)
        -> GraphicsBackendLoader&;

    //! Destructor. Unloads the backend if it was loaded.
    ~GraphicsBackendLoader();

    OXYGEN_MAKE_NON_COPYABLE(GraphicsBackendLoader)
    OXYGEN_MAKE_NON_MOVABLE(GraphicsBackendLoader)

    //! Loads the specified graphics backend from a dynamically loadable module,
    //! and constructs an instance of it using the provided configuration. There
    //! will always be a single instance of the backend loaded at any given
    //! time.
    /*!
     \return A weak pointer to the loaded graphics backend. If the backend could
     not be loaded, the returned pointer will be empty. If at any point the
     backend is unloaded, the returned pointer will expire and become unusable.
    */
    [[nodiscard]] auto LoadBackend(graphics::BackendType backend, const GraphicsConfig& config) const
        -> std::weak_ptr<Graphics>;

    //! Unloads the currently loaded graphics backend, destroying its instance
    //! and as a result, rendering all weak pointers to it unusable. The
    //! module's reference count is decremented, and if it is no longer
    //! referenced, it is automatically unloaded.
    void UnloadBackend() const noexcept;

    //! Gets the backend instance if one is currently loaded.
    /*!
     \return A weak pointer to the currently loaded graphics backend, or an
     empty pointer if no backend is loaded. The pointer will expire if the
     backend is unloaded at a later time.
    */
    [[nodiscard]] auto GetBackend() const noexcept -> std::weak_ptr<Graphics>;

private:
    explicit GraphicsBackendLoader(std::shared_ptr<loader::detail::PlatformServices> platform_services);

    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace oxygen
