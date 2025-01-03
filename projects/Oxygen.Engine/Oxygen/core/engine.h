//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <list>
#include <memory>
#include <string>

// #include <vulkan/vulkan_core.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Base/MixinShutdown.h"
#include "Oxygen/Base/Time.h"
#include "Oxygen/Graphics/Common/Types.h"
#include "Oxygen/ImGui/ImGuiRenderInterface.h"
#include "Oxygen/ImGui/ImguiModule.h"
#include "Oxygen/Platform/Common/Types.h"
#include "Oxygen/api_export.h"

namespace oxygen {

constexpr uint64_t kDefaultFixedUpdateDuration { 200'000 };
constexpr uint64_t kDefaultFixedIntervalDuration { 20'000 };

namespace core {
  class Module;
} // namespace core

class Engine
  : public Mixin<Engine, Curry<MixinNamed, const char*>::mixin, MixinInitialize, MixinShutdown>,
    public std::enable_shared_from_this<Engine>
{
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

  //! Constructor to forward the arguments to the mixins in the chain.
  template <typename... Args>
  explicit Engine(PlatformPtr platform, GraphicsPtr graphics, Properties props, Args&&... args)
    : Mixin(Name().c_str(), std::forward<Args>(args)...)
    , platform_(std::move(platform))
    , graphics_(std::move(graphics))
    , props_(std::move(props))
  {
  }

  OXYGEN_API ~Engine() override = default;

  OXYGEN_MAKE_NON_COPYABLE(Engine);
  OXYGEN_MAKE_NON_MOVEABLE(Engine);

  OXYGEN_API [[nodiscard]] auto GetPlatform() const -> Platform&;

  //! Attaches the given Module to the engine, to be updated, rendered, etc.
  //! \param module module to be attached.
  //! \param priority layer to determine the order of invocation. Default is the main layer (`0`).
  //! \throws std::invalid_argument if the module is attached or the weak_ptr is expired.
  OXYGEN_API void AttachModule(const ModulePtr& module, uint32_t priority = 0);

  OXYGEN_API auto Run() -> void;

  OXYGEN_API [[nodiscard]] static auto Name() -> const std::string&;
  OXYGEN_API [[nodiscard]] static auto Version() -> uint32_t;

  [[nodiscard]] auto HasImGui() const -> bool { return imgui_module_ != nullptr; }
  OXYGEN_API [[nodiscard]] auto GetImGuiRenderInterface() const -> imgui::ImGuiRenderInterface;

 private:
  OXYGEN_API virtual void OnInitialize();
  template <typename Base, typename... CtorArgs>
  friend class MixinInitialize; //< Allow access to OnInitialize.

  OXYGEN_API virtual void OnShutdown();
  template <typename Base>
  friend class MixinShutdown; //< Allow access to OnShutdown.

  //! Detach the given Module from the engine.
  //! @param module the module to be detached.
  //! \throws std::invalid_argument if the module is not attached or the weak_ptr is expired.
  void DetachModule(const ModulePtr& module);

  PlatformPtr platform_;
  GraphicsPtr graphics_;
  Properties props_;
  std::unique_ptr<imgui::ImguiModule> imgui_module_ {};

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

  void SortModulesByPriority();
  void InitializeModules();
  void ShutdownModules();
};

} // namespace oxygen
