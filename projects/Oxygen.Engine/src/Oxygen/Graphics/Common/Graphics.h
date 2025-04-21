// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Core/Types.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen {

namespace graphics {
    class IShaderByteCode;
} // namespace graphics

namespace imgui {
    class ImguiModule;
} // namespace imgui

class Graphics : public Composition {
public:
    explicit Graphics(const char* name)
    {
        AddComponent<ObjectMetaData>(name);
    }

    ~Graphics() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Graphics);
    OXYGEN_DEFAULT_MOVABLE(Graphics);

    OXYGEN_GFX_API auto StartAsync(co::TaskStarted<> started = {}) -> co::Co<>;
    OXYGEN_GFX_API virtual void Run();

    [[nodiscard]] auto GetName() const noexcept -> std::string_view
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    [[nodiscard]] auto IsWithoutRenderer() const { return is_renderer_less_; }

    [[nodiscard]] virtual OXYGEN_GFX_API auto GetShader(std::string_view unique_id) const
        -> std::shared_ptr<graphics::IShaderByteCode>
        = 0;

    //! Get the renderer instance for this graphics backend.
    /*!
      \return A weak pointer to the renderer, which expires if the backend module
      gets unloaded.

      There can be only one instance of the render for a graphics backend. That
      instance is lazily created on the first call to `GetRenderer()`. This is by
      design as in certain scenarios the backend is created and used
      renderer-less.

      Once created, the instance stays alive for as long as the graphics backend
      is not shutdown. It can always be obtained by calling `GetRenderer()`.
    */
    [[nodiscard]] OXYGEN_GFX_API auto GetRenderer() const noexcept -> const graphics::Renderer*;
    [[nodiscard]] OXYGEN_GFX_API auto GetRenderer() noexcept -> graphics::Renderer*;

    //! Initialize command queues using the provided queue strategy.
    /*!
     \param queue_strategy The queue strategy to use for initializing command
      queues.
     \return A vector of command queues created by the backend.
    */
    [[nodiscard]] OXYGEN_GFX_API void CreateCommandQueues(
        const graphics::QueueStrategy& queue_strategy);

    //! Get a command queue by its unique name.
    /*!
     \param name The unique name of the queue as specified in QueueSpecification
      when the application queue management strategy was defined.
     \return A pointer to the command queue, or nullptr if not found.
    */
    [[nodiscard]] OXYGEN_GFX_API auto GetCommandQueue(std::string_view name) const
        -> std::shared_ptr<graphics::CommandQueue>;

    [[nodiscard]]
    virtual auto CreateImGuiModule(EngineWeakPtr engine, platform::WindowIdType window_id) const
        -> std::unique_ptr<imgui::ImguiModule>
        = 0;

    [[nodiscard]] virtual OXYGEN_GFX_API auto CreateSurface(const platform::Window& window) const -> std::unique_ptr<graphics::Surface> = 0;

protected:
    //     //! Initialize the graphics backend module.
    //     virtual void InitializeGraphicsBackend(const SerializedBackendConfig& props) = 0;
    //     //! Shutdown the graphics backend module.
    //     virtual void ShutdownGraphicsBackend() = 0;

    //! Create a renderer for this graphics backend.
    [[nodiscard]] virtual auto CreateRenderer() -> std::unique_ptr<graphics::Renderer> = 0;

    //! Create a command queue for the given role and allocation preference.
    /*!
     \param role The role of the command queue.
     \param allocation_preference The allocation preference for the command queue.
     \return A shared pointer to the created command queue.
     \throw std::runtime_error if the command queue could not be created.

     This method is called by the graphics backend to create a command queue for
     a specific role and allocation preference. The backend implementation is
     responsible for mapping these parameters to API-specific queue types or
     families.
    */
    [[nodiscard]] virtual auto CreateCommandQueue(graphics::QueueRole role, graphics::QueueAllocationPreference allocation_preference)
        -> std::shared_ptr<graphics::CommandQueue>
        = 0;

    [[nodiscard]] auto Nursery() const -> co::Nursery&
    {
        DCHECK_NOTNULL_F(nursery_);
        return *nursery_;
    }

private:
    // OXYGEN_GFX_API virtual void OnInitialize(const SerializedBackendConfig& props);
    // template <typename Base, typename... CtorArgs>
    // friend class MixinInitialize; //< Allow access to OnInitialize.

    // OXYGEN_GFX_API virtual void OnShutdown();
    // template <typename Base>
    // friend class MixinShutdown; //< Allow access to OnShutdown.

    PlatformPtr platform_ {}; //< The platform abstraction layer.

    bool is_renderer_less_ { true }; //< Indicates if the backend is renderer-less.
    std::shared_ptr<graphics::Renderer> renderer_ {}; //< The renderer instance.

    //! The command queues created by the backend.
    std::unordered_map<std::string, std::shared_ptr<graphics::CommandQueue>> command_queues_ {};

    co::Nursery* nursery_ { nullptr };
};

} // namespace oxygen
