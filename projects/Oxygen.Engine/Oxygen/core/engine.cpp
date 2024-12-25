//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/core/engine.h"

#include <algorithm>
#include <ranges>

#if 0
#  include <vulkan/vulkan_core.h>

#  include "oxygen/engine/physical_device.h"
#  include "oxygen/vulkan/details/vk_utils.h"
#  include "oxygen/vulkan/instance.h"
#endif

#include "module.h"
#include "oxygen/base/logging.h"
#include "oxygen/base/Time.h"
#include "oxygen/platform/platform.h"
#include "version.h"

using oxygen::Engine;
// using oxygen::core::DeviceRequirements;
// using oxygen::core::PhysicalDevice;
// using oxygen::core::PhysicalDeviceType;
// using oxygen::core::SuitableDevice;
// using oxygen::vulkan::Instance;
// using oxygen::vulkan::details::CheckVk;
// using oxygen::vulkan::details::GetDeviceFeatureDescription;

#if 0
namespace {

  auto GetDeviceFeaturesAsVector(const VkPhysicalDeviceFeatures& features)
    -> std::vector<VkBool32>
  {
    std::vector<VkBool32> comparable_features(
      sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32));
    std::memcpy(
      comparable_features.data(),
      &features,
      sizeof(VkPhysicalDeviceFeatures));
    return comparable_features;
  }

  auto IsExtensionSupported(
    const std::vector<VkExtensionProperties>& extensions,
    const std::string& extension_name) -> bool
  {
    return std::ranges::find_if(
      extensions,
      [&](const VkExtensionProperties& extension) {
        return extension.extensionName == extension_name;
      }) != extensions.end();
  }

  auto ScorePhysicalDevice(
    PhysicalDevice const& device,
    DeviceRequirements const& requirements,
    VkPhysicalDeviceFeatures& features_to_enable,
    std::vector<const char*>& extensions_to_enable) -> uint32_t
  {
    uint32_t score = 0;

    // Check extensions
    // Checks if the requested extensions are supported.
    uint32_t count{ 0 };
    CheckVk(
      vkEnumerateDeviceExtensionProperties(
        device.VkHandle(),
        nullptr,
        &count,
        nullptr),
      "vkEnumerateDeviceExtensionProperties failed");
    std::vector<VkExtensionProperties> device_extensions(count);
    CheckVk(
      vkEnumerateDeviceExtensionProperties(
        device.VkHandle(),
        nullptr,
        &count,
        device_extensions.data()),
      "vkEnumerateDeviceExtensionProperties failed");

    for (auto const& extension : requirements.required_extensions) {
      if (!IsExtensionSupported(device_extensions, extension)) {
        LOG(WARNING) << "Device [" << device.Name()
          << "] does not support required extension '" << extension
          << "'";
        return 0;
      }
      extensions_to_enable.emplace_back(extension);
    }

    for (auto const& extension : requirements.optional_extensions) {
      if (!IsExtensionSupported(device_extensions, extension)) {
        LOG(WARNING) << "Device [" << device.Name()
          << "] does not support optional extension '" << extension
          << "'";
      }
      else {
        extensions_to_enable.emplace_back(extension);
        score += 10;
      }
    }

    // Check features

    VkPhysicalDeviceFeatures available_features{};
    vkGetPhysicalDeviceFeatures(device.VkHandle(), &available_features);

    auto const v_required_features =
      GetDeviceFeaturesAsVector(requirements.required_features);
    auto const v_optional_features =
      GetDeviceFeaturesAsVector(requirements.optional_features);
    auto const v_available_features =
      GetDeviceFeaturesAsVector(available_features);

    constexpr auto kFeatureCount =
      sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
    std::vector v_features_to_enable(kFeatureCount, VK_FALSE);

    for (std::size_t i = 0; i < kFeatureCount; i++) {
      if (v_required_features[i] == VK_TRUE) {
        if (v_available_features[i] == VK_TRUE) {
          v_features_to_enable[i] = VK_TRUE;
        }
        else {
          LOG(WARNING) << "The physical device [" << device.Name()
            << "] does not support required feature '"
            << GetDeviceFeatureDescription(i) << "'";
          return 0;
        }
      }
      if (v_optional_features[i] == VK_TRUE) {
        if (v_available_features[i] == VK_TRUE) {
          v_features_to_enable[i] = VK_TRUE;
          score += 10;
        }
        else {
          DLOG(WARNING) << "The physical device [" << device.Name()
            << "] does not support optional feature '"
            << GetDeviceFeatureDescription(i) << "'";
        }
      }
    }
    std::memcpy(
      &features_to_enable,
      v_features_to_enable.data(),
      v_features_to_enable.size());

    // Add a score boost for GPUs
    if (device.DeviceType() == PhysicalDeviceType::kDiscrete) {
      score += 1000;
    }
    else if (
      device.DeviceType() == PhysicalDeviceType::kIntegrated ||
      device.DeviceType() == PhysicalDeviceType::kVirtualGpu) {
      score += 100;
    }

    // Gives a higher score to devices with a higher maximum texture size.
    score += device.Limits().maxImageDimension2D;

    // Give a higher score for devices with more memory available
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(device.VkHandle(), &memory_properties);

    return score;
  }

}  // namespace
#endif

Engine::Engine(PlatformPtr platform, Properties props)
  : platform_(std::move(platform))
  ,
#if 0
  instance_(std::make_unique<Instance>(Instance::Properties{
      .application =
          {
              props.application.name,
              props.application.version,
          },
      .engine =
          {
              Name(),
              Version(),
          },
      .extensions = props.extensions,
                                       })),
#endif
                                       props_(std::move(props)) {
  // DiscoverDevices();
  LOG_F(INFO, "Engine initialization complete");
}

oxygen::Engine::~Engine() {
  // devices_.clear();
  // instance_.reset();
  LOG_F(INFO, "Engine destroyed");
}

auto Engine::GetPlatform() const -> Platform& {
  return *platform_;
}

#if 0
auto Engine::GetInstance() const -> VkInstance const&
{
  return instance_->VkHandle();
}
#endif

void Engine::AddModule(std::weak_ptr<core::Module> module) {
  modules_.push_back(ModuleContext{ .module = std::move(module) });
}

auto Engine::Run() -> void {
  bool continue_running{ true };

  // Listen for the last window closed event
  auto last_window_closed_con = GetPlatform().OnLastWindowClosed().connect(
    [&continue_running]() { continue_running = false; });

  std::ranges::for_each(
    modules_,
    [](auto& module) {
      if (auto the_module = module.module.lock()) {
        the_module->Initialize();
      }
    });

  // Start the master clock
  engine_clock_.Reset();

  // https://gafferongames.com/post/fix_your_timestep/
  std::ranges::for_each(
    modules_,
    [](auto& module)
    {
      module.frame_time.Reset();
    });

  while (continue_running) {
    auto event = GetPlatform().PollEvent();
    std::ranges::for_each(
      modules_, [this, &continue_running, &event](auto& module) {
        if (auto the_module = module.module.lock()) {
          // Inputs
          if (event) {
            the_module->ProcessInput(*event);
          }

          if (continue_running) {
            module.frame_time.Update();
            auto delta = module.frame_time.Delta();

            // Fixed updates
            if (delta > props_.max_fixed_update_duration) {
              delta = props_.max_fixed_update_duration;
            }
            module.fixed_accumulator += module.frame_time.Delta();
            while (module.fixed_accumulator >= module.fixed_interval) {
              the_module->FixedUpdate(
                //  module.time_since_start.ElapsedTime(),
                // module.fixed_interval
              );
              module.fixed_accumulator -= module.fixed_interval;
              module.ups.Update();
            }
            // TODO(abdessattar): Interpolate the remaining time in the
            // accumulator const float alpha =
            //    static_cast<float>(module.fixed_accumulator.count()) /
            //    static_cast<float>(module.fixed_interval.count());
            // the_module->FixedUpdate(/*alpha*/);

            // Per frame updates / render
            the_module->Update(module.frame_time.Delta());
            the_module->Render();
            module.fps.Update();

            // Log FPS and UPS once every second
            if (module.log_timer.ElapsedTime() >= std::chrono::seconds(1)) {
              LOG_F(INFO, "FPS: {} UPS: {}", module.fps.Value(), module.ups.Value());
              module.log_timer = {};
            }
          }
        }
      });
  }
  LOG_F(INFO, "Engine stopped.");

  std::ranges::for_each(
    modules_,
    [](auto& module)
    {
      if (auto the_module = module.module.lock()) {
        the_module->Shutdown();
      }
    });

  // Stop listening for the last window closed event
  last_window_closed_con.disconnect();
}

auto oxygen::Engine::Name() -> const std::string& {
  static const std::string kName{ "Oxygen" };
  return kName;
}

auto oxygen::Engine::Version() -> uint32_t {
  constexpr uint32_t kBitsPatch{ 12 };
  constexpr uint32_t kBitsMinor{ 10 };
  return (static_cast<uint32_t>(version::Major()) << (kBitsPatch + kBitsMinor))
    | ((static_cast<uint32_t>(version::Minor())) << kBitsPatch)
    | (static_cast<uint32_t>(version::Patch()));
}

#if 0
auto Engine::DiscoverDevices() -> void
{
  uint32_t count = 0;
  CheckVk(
    vkEnumeratePhysicalDevices(instance_->VkHandle(), &count, nullptr),
    "vkEnumeratePhysicalDevices failed");
  std::vector<VkPhysicalDevice> vk_devices(count);
  CheckVk(
    vkEnumeratePhysicalDevices(
      instance_->VkHandle(),
      &count,
      vk_devices.data()),
    "vkEnumeratePhysicalDevices failed");

  devices_.reserve(vk_devices.size());
  std::ranges::transform(
    vk_devices,
    std::inserter(devices_, devices_.end()),
    [](VkPhysicalDevice const& vk_device) {
      return std::make_shared<PhysicalDevice>(vk_device);
    });

  // TODO(abdessattar): populate queue families when discovering devices not
  // when selecting
}

auto Engine::SelectDevices(DeviceRequirements const& requirements) const
-> std::vector<SuitableDevice>
{
  std::vector<SuitableDevice> suitable_devices;

  std::ranges::transform(
    devices_,
    std::inserter(suitable_devices, suitable_devices.end()),
    [&requirements](auto const& device) {
      if (SuitableDevice device_option{
              .device = device,
              .features = {},
              .extensions = {},
          };
          device->PopulateQueueFamilyIndices(
            requirements.get_presentation_support_cb) &&
          ScorePhysicalDevice(
            *device,
            requirements,
            device_option.features,
            device_option.extensions) > 0) {
        return device_option;
      }
      return SuitableDevice{};
    });

  return suitable_devices;
}

auto Engine::SelectDevice(DeviceRequirements const& requirements) const
-> SuitableDevice
{
  // Maps to hold devices and sort by rank.
  std::multimap<uint32_t, SuitableDevice> ranked_devices;

  std::ranges::transform(
    devices_,
    std::inserter(ranked_devices, ranked_devices.end()),
    [&requirements](auto const& device) {
      SuitableDevice device_option{
          .device = device,
          .features = {},
          .extensions = {},
      };
      auto score = device->PopulateQueueFamilyIndices(
        requirements.get_presentation_support_cb)
        ? ScorePhysicalDevice(
          *device,
          requirements,
          device_option.features,
          device_option.extensions)
        : 0;
      return std::make_pair(score, std::move(device_option));
    });

  // Checks to make sure the best candidate scored higher than 0.
  // rbegin() points to the last element in the ranked devices (ordered from
  // lowest ranked to highest ranked); the key is the score.
  if (ranked_devices.rbegin()->first > 0) {
    return std::move(ranked_devices.rbegin()->second);
  }
  return SuitableDevice{};
}
#endif
