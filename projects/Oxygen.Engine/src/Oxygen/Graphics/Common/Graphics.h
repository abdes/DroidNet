// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
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
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ParkingLot.h>
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
    OXYGEN_GFX_API explicit Graphics(std::string_view name);
    OXYGEN_GFX_API ~Graphics() override;

    OXYGEN_MAKE_NON_COPYABLE(Graphics);
    OXYGEN_DEFAULT_MOVABLE(Graphics);

    [[nodiscard]] OXYGEN_GFX_API auto ActivateAsync(co::TaskStarted<> started) -> co::Co<> override;
    OXYGEN_GFX_API void Run() override;
    [[nodiscard]] OXYGEN_GFX_API auto IsRunning() const -> bool override;
    OXYGEN_GFX_API void Stop() override;

    [[nodiscard]] auto GetName() const noexcept -> std::string_view
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    void Render()
    {
        render_.UnParkAll();
    }

    auto RenderStart()
    {
        return render_.Park();
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
    OXYGEN_GFX_API void CreateCommandQueues(const graphics::QueueStrategy& queue_strategy);

    [[nodiscard]] OXYGEN_GFX_API auto GetCommandQueue(std::string_view name) const
        -> std::shared_ptr<graphics::CommandQueue>;

    [[nodiscard]] OXYGEN_GFX_API auto AcquireCommandList(
        graphics::QueueRole queue_role,
        std::string_view command_list_name)
        -> std::shared_ptr<graphics::CommandList>;

    [[nodiscard]] virtual auto CreateImGuiModule(EngineWeakPtr engine, platform::WindowIdType window_id) const
        -> std::unique_ptr<imgui::ImguiModule>
        = 0;

    [[nodiscard]] virtual OXYGEN_GFX_API auto CreateSurface(
        std::weak_ptr<platform::Window> window_weak,
        std::shared_ptr<graphics::CommandQueue> command_queue) const
        -> std::shared_ptr<graphics::Surface>
        = 0;
    [[nodiscard]] virtual OXYGEN_GFX_API auto CreateRenderer(
        std::string_view name,
        std::weak_ptr<graphics::Surface> surface,
        uint32_t frames_in_flight)
        -> std::shared_ptr<graphics::Renderer>;

protected:
    //! Create a command queue for the given role and allocation preference.
    /*!
     \param queue_name The debug name for this queue.
     \param role The role of the command queue.
     \param allocation_preference The allocation preference for the command queue.
     \return A shared pointer to the created command queue.
     \throw std::runtime_error If the command queue could not be created.

     This method is called by the graphics backend to create a command queue for
     a specific role and allocation preference. The backend implementation is
     responsible for mapping these parameters to API-specific queue types or
     families.
    */
    [[nodiscard]] virtual auto CreateCommandQueue(
        std::string_view queue_name,
        graphics::QueueRole role,
        graphics::QueueAllocationPreference allocation_preference)
        -> std::shared_ptr<graphics::CommandQueue>
        = 0;

    /**
     * Creates a new command list for the given queue role.
     * For internal use by the command list pool.
     */
    [[nodiscard]] virtual auto CreateCommandListImpl(
        graphics::QueueRole role,
        std::string_view command_list_name)
        -> std::unique_ptr<graphics::CommandList>
        = 0;

    [[nodiscard]] auto Nursery() const -> co::Nursery&
    {
        DCHECK_NOTNULL_F(nursery_);
        return *nursery_;
    }
    [[nodiscard]] virtual auto CreateRendererImpl(
        std::string_view name,
        std::weak_ptr<graphics::Surface> surface,
        uint32_t frames_in_flight)
        -> std::unique_ptr<graphics::Renderer>
        = 0;

private:
    /**
     * Create a command recorder for an existing command list.
     * The command recorder will automatically handle Begin() on creation and End() when destroyed.
     *
     * @param command_list The command list to create a recorder for
     * @return A unique_ptr to CommandRecorder with custom deleter for automatic Begin/End handling
     */
    [[nodiscard]] OXYGEN_GFX_API auto CreateCommandRecorder(
        graphics::CommandList* command_list)
        -> std::unique_ptr<graphics::CommandRecorder, std::function<void(graphics::CommandRecorder*)>>;

    PlatformPtr platform_ {}; //< The platform abstraction layer.

    //! The command queues created by the backend.
    std::unordered_map<std::string, std::shared_ptr<graphics::CommandQueue>> command_queues_ {};

    // Pool of available command lists by queue type
    std::unordered_map<graphics::QueueRole, std::vector<std::unique_ptr<graphics::CommandList>>> command_list_pool_;
    std::mutex command_list_pool_mutex_;

    //! Active renderers managed by this Graphics instance
    std::vector<std::weak_ptr<graphics::Renderer>> renderers_ {};

    co::Nursery* nursery_ { nullptr };
    co::ParkingLot render_ {};
};

} // namespace oxygen
