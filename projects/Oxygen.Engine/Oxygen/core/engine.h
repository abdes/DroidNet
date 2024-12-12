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

#include "oxygen/api_export.h"
#include "oxygen/base/time.h"
#include "oxygen/platform/fwd.h"

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

    OXYGEN_API Engine(Platform& platform, Properties props);
    OXYGEN_API ~Engine();

    // Non-copyable
    Engine(const Engine&) = delete;
    auto operator=(const Engine&)->Engine & = delete;

    // Non-Movable
    Engine(Engine&& other) noexcept = delete;
    auto operator=(Engine&& other) noexcept -> Engine & = delete;

    OXYGEN_API [[nodiscard]] auto GetPlatform() const->Platform&;

    OXYGEN_API void AddModule(std::weak_ptr<core::Module> module);

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

    Properties props_;
    Platform& platform_;

    DeltaTimeCounter engine_clock_{};

    struct ModuleContext
    {
      std::weak_ptr<core::Module> module;
      Duration fixed_interval{ kDefaultFixedIntervalDuration };
      Duration fixed_accumulator{};
      ElapsedTimeCounter time_since_start{};
      DeltaTimeCounter frame_time{};
      ChangePerSecondCounter fps{};
      ChangePerSecondCounter ups{};
    };
    std::vector<ModuleContext> modules_;
  };

}  // namespace oxygen
