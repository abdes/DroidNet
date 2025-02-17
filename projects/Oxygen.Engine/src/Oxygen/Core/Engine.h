//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <string>

// #include <vulkan/vulkan_core.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/TimeUtils.h>
#include <Oxygen/Core/api_export.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/ImGui/ImGuiRenderInterface.h>
#include <Oxygen/ImGui/ImguiModule.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen {

constexpr uint64_t kDefaultFixedUpdateDuration { 200'000 };
constexpr uint64_t kDefaultFixedIntervalDuration { 20'000 };

namespace core {
    class Module;
} // namespace core

class Engine : public std::enable_shared_from_this<Engine> {
public:
    using ModulePtr = std::shared_ptr<core::Module>;

    struct Properties {
        struct
        {
            std::string name;
            uint32_t version;
        } application;
        std::vector<const char*> extensions; // Vulkan instance extensions
        Duration max_fixed_update_duration { kDefaultFixedUpdateDuration };
        bool enable_imgui_layer { true };
        platform::WindowIdType main_window_id {};
    };

    OXYGEN_CORE_API Engine(PlatformPtr platform, GraphicsPtr graphics, Properties props);

    OXYGEN_CORE_API ~Engine() noexcept;

    OXYGEN_MAKE_NON_COPYABLE(Engine)
    OXYGEN_MAKE_NON_MOVABLE(Engine)

    [[nodiscard]] OXYGEN_CORE_API auto GetPlatform() const -> Platform&;

    //! Attaches the given Module to the engine, to be updated, rendered, etc.
    //! \param module module to be attached.
    //! \param layer layer to determine the order of invocation. Default is the main layer (`0`).
    //! \throws std::invalid_argument if the module is attached or the weak_ptr is expired.
    OXYGEN_CORE_API void AttachModule(const ModulePtr& module, uint32_t layer = 0);

    OXYGEN_CORE_API auto Run() -> void;
    constexpr auto IsRunning() const -> bool { return is_running_; }
    OXYGEN_CORE_API void Stop();

    [[nodiscard]] OXYGEN_CORE_API static auto Name() -> const std::string&;
    [[nodiscard]] OXYGEN_CORE_API static auto Version() -> uint32_t;

    [[nodiscard]] auto HasImGui() const -> bool { return imgui_module_ != nullptr; }
    [[nodiscard]] OXYGEN_CORE_API auto GetImGuiRenderInterface() const -> imgui::ImGuiRenderInterface;

private:
    //! Detach the given Module from the engine.
    //! @param module the module to be detached.
    //! \throws std::invalid_argument if the module is not attached or the weak_ptr is expired.
    void DetachModule(const ModulePtr& module);

    PlatformPtr platform_;
    GraphicsPtr graphics_;
    Properties props_;
    std::unique_ptr<imgui::ImguiModule> imgui_module_ {};
    bool is_running_ { false };
    std::atomic_bool is_stop_requested_ { false };

    DeltaTimeCounter engine_clock_ {};

    struct ModuleContext {
        ModulePtr module;
        uint32_t layer;
        Duration fixed_interval { kDefaultFixedIntervalDuration };
        Duration fixed_accumulator {};
        ElapsedTimeCounter time_since_start {};
        DeltaTimeCounter frame_time {};
        ChangePerSecondCounter fps {};
        ChangePerSecondCounter ups {};
        ElapsedTimeCounter log_timer {}; // Add this timer
    };
    std::list<ModuleContext> modules_;

    void ReorderLayers();
    void InitializeModules();
    void ShutdownModules() noexcept;
    void InitializeImGui();
    void ShutdownImGui() noexcept;
};

} // namespace oxygen
