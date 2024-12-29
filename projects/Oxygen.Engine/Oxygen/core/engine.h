//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

// #include <vulkan/vulkan_core.h>

#include <list>

#include "oxygen/api_export.h"
#include "oxygen/base/Time.h"
#include "oxygen/platform/Types.h"
#include "Oxygen/Renderers/Common/Types.h"

namespace oxygen {

  constexpr uint64_t kDefaultFixedUpdateDuration{ 200'000 };
  constexpr uint64_t kDefaultFixedIntervalDuration{ 20'000 };

  namespace core {
    class Module;
  }  // namespace core

#if 0
  namespace engine {

    struct DeviceRequirements
    {
      // Signature of the callback function that can be used to check if a
      // particular device queue family supports presentation.
      using GetPresentationSupportCallback =
        std::function<bool(VkPhysicalDevice, uint32_t queue_family_index)>;

      std::vector<const char*> required_extensions;

      // Devices that support these extensions will be preferred.
      std::vector<const char*> optional_extensions;

      VkPhysicalDeviceFeatures required_features;
      VkPhysicalDeviceFeatures optional_features;

      // TODO(abdessattar) add requirements on queue families

      // Optional callback, when provided, it will be used to check if a
      // particular device queue family supports presentation to the surface for
      // which a device is being selected.
      GetPresentationSupportCallback get_presentation_support_cb;
    };

    struct SuitableDevice
    {
      std::weak_ptr<PhysicalDevice> device;

      // These are the device features and extensions that are both supported by
      // the device and requested during device selection either as required or as
      // optional. They can be used when creating a logical device on top of this
      // physical device.

      VkPhysicalDeviceFeatures features;
      std::vector<const char*> extensions{};
    };

  }  // namespace core
#endif

  class Engine
  {
  public:
    using ModulePtr = std::shared_ptr<core::Module>;

    struct Properties
    {
      struct
      {
        std::string name;
        uint32_t version;
      } application;
      std::vector<const char*> extensions;  // Vulkan instance extensions
      Duration max_fixed_update_duration{ kDefaultFixedUpdateDuration };
    };

    OXYGEN_API Engine(PlatformPtr platform, RendererPtr renderer, Properties props);
    OXYGEN_API ~Engine();

    // Non-copyable
    Engine(const Engine&) = delete;
    auto operator=(const Engine&)->Engine & = delete;

    // Non-Movable
    Engine(Engine&& other) noexcept = delete;
    auto operator=(Engine&& other) noexcept -> Engine & = delete;

    OXYGEN_API [[nodiscard]] auto GetPlatform() const->Platform&;

    //! Attaches the given Module to the engine, to be updated, rendered, etc.
    //! \param module module to be attached.
    //! \param layer layer to determine the order of invocation. Default is the main layer (`0`).
    //! \throws std::invalid_argument if the module is attached or the weak_ptr is expired.
    OXYGEN_API void AttachModule(const ModulePtr& module, uint32_t layer = 0);

    //! Detached the given Module from the engine.
    //! @param module the module to be detached.
    //! \throws std::invalid_argument if the module is not attached or the weak_ptr is expired.
    OXYGEN_API void DetachModule(const ModulePtr& module);

    OXYGEN_API auto Run() -> void;

#if 0
    [[nodiscard]] auto GetInstance() const->VkInstance const&;

    [[nodiscard]] OXYGEN_API auto SelectDevices(
      engine::DeviceRequirements const& requirements) const
      ->std::vector<engine::SuitableDevice>;

    [[nodiscard]] OXYGEN_API auto SelectDevice(
      engine::DeviceRequirements const& requirements) const
      ->engine::SuitableDevice;
#endif

    [[nodiscard]] static auto Name() -> const std::string&;
    [[nodiscard]] static auto Version() -> uint32_t;

  private:
#if 0
    std::unique_ptr<vulkan::Instance> instance_;

    std::vector<std::shared_ptr<engine::PhysicalDevice>> devices_;
    auto DiscoverDevices() -> void;
#endif

    PlatformPtr platform_;
    RendererPtr renderer_;
    Properties props_;

    DeltaTimeCounter engine_clock_{};

    struct ModuleContext
    {
      ModulePtr module;
      uint32_t layer;
      Duration fixed_interval{ kDefaultFixedIntervalDuration };
      Duration fixed_accumulator{};
      ElapsedTimeCounter time_since_start{};
      DeltaTimeCounter frame_time{};
      ChangePerSecondCounter fps{};
      ChangePerSecondCounter ups{};
      ElapsedTimeCounter log_timer{}; // Add this timer
    };
    std::list<ModuleContext> modules_;

    void SortModulesByLayer();
    void InitializeModules();
    void ShutdownModules();
  };

}  // namespace oxygen
