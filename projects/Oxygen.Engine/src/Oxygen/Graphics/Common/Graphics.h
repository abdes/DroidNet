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
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Platform/Types.h>


namespace oxygen {

namespace graphics {
    class IShaderByteCode;
    namespace detail {
        class RenderThread;
    } // namespace detail
} // namespace graphics

namespace imgui {
    class ImguiModule;
} // namespace imgui

class Graphics : public Composition, public co::LiveObject {
public:
    OXYGEN_GFX_API explicit Graphics(const std::string_view name);
    OXYGEN_GFX_API ~Graphics() override;

    OXYGEN_MAKE_NON_COPYABLE(Graphics);
    OXYGEN_DEFAULT_MOVABLE(Graphics);

    [[nodiscard]] OXYGEN_GFX_API auto ActivateAsync(co::TaskStarted<> started = {}) -> co::Co<>;
    OXYGEN_GFX_API void Run();
    [[nodiscard]] OXYGEN_GFX_API auto IsRunning() const -> bool;
    OXYGEN_GFX_API void Stop();

    [[nodiscard]] auto GetName() const noexcept -> std::string_view
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    [[nodiscard]] virtual OXYGEN_GFX_API auto GetShader(std::string_view unique_id) const
        -> std::shared_ptr<graphics::IShaderByteCode>
        = 0;

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

    [[nodiscard]] virtual OXYGEN_GFX_API auto CreateSurface(std::weak_ptr<platform::Window> window_weak, std::shared_ptr<graphics::CommandQueue> command_queue) const -> std::shared_ptr<graphics::Surface> = 0;
    [[nodiscard]] virtual OXYGEN_GFX_API auto CreateRenderer(const std::string_view name, std::shared_ptr<graphics::Surface> surface, uint32_t frames_in_flight = oxygen::kFrameBufferCount - 1) -> std::shared_ptr<graphics::Renderer>;

protected:
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

protected:
    [[nodiscard]] auto Nursery() const -> co::Nursery&
    {
        DCHECK_NOTNULL_F(nursery_);
        return *nursery_;
    }
    [[nodiscard]] virtual auto CreateRendererImpl(const std::string_view name, std::shared_ptr<graphics::Surface> surface, uint32_t frames_in_flight) -> std::shared_ptr<graphics::Renderer> = 0;

private:
    PlatformPtr platform_ {}; //< The platform abstraction layer.

    //! The command queues created by the backend.
    std::unordered_map<std::string, std::shared_ptr<graphics::CommandQueue>> command_queues_ {};

    //! Active renderers managed by this Graphics instance
    std::vector<std::shared_ptr<graphics::Renderer>> renderers_ {};

    co::Nursery* nursery_ { nullptr };
};

} // namespace oxygen
