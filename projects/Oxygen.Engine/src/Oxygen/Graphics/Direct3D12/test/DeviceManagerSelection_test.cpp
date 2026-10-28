//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>
#include <Oxygen/Testing/GTest.h>

extern "C" auto GetGraphicsModuleApi() -> void*;

namespace oxygen::graphics::d3d12 {
namespace {

  struct DiscoveredAdapter {
    std::string name;
    std::uint32_t device_id { 0U };
  };

  //! A plain device: no debug layer or Aftermath, which add nothing to
  //! adapter selection.
  auto MakeDesc() -> DeviceManagerDesc
  {
    return DeviceManagerDesc {
      .enable_debug_layer = false,
      .enable_aftermath = false,
    };
  }

  //! The adapters this machine can select, discovered without selecting one.
  auto DiscoverUsableAdapters() -> std::vector<DiscoveredAdapter>
  {
    auto desc = MakeDesc();
    desc.auto_select_adapter = false;
    const auto manager = DeviceManager(desc);
    auto adapters = std::vector<DiscoveredAdapter> {};
    for (const auto& adapter : manager.Adapters()) {
      if (adapter.MeetsFeatureLevel()) {
        adapters.push_back({
          .name = adapter.Name(),
          .device_id = adapter.DeviceId(),
        });
      }
    }
    return adapters;
  }

  //! The adapter `SelectBestAdapter` selected, which must have a device.
  auto SelectedAdapter(const DeviceManager& manager)
    -> std::optional<DiscoveredAdapter>
  {
    if (manager.Device() == nullptr) {
      return std::nullopt;
    }
    for (const auto& adapter : manager.Adapters()) {
      if (adapter.IsBest()) {
        return DiscoveredAdapter {
          .name = adapter.Name(),
          .device_id = adapter.DeviceId(),
        };
      }
    }
    return std::nullopt;
  }

  //! Without hints, a usable adapter is selected.
  NOLINT_TEST(DeviceManagerSelectionTest, DefaultSelectsAUsableAdapter)
  {
    const auto usable = DiscoverUsableAdapters();
    ASSERT_FALSE(usable.empty());

    const auto manager = DeviceManager(MakeDesc());

    const auto selected = SelectedAdapter(manager);
    ASSERT_HAS_VALUE(selected);
    EXPECT_TRUE(std::ranges::any_of(
      usable, [&selected](const DiscoveredAdapter& adapter) -> bool {
        return adapter.device_id == selected->device_id;
      }));
  }

  //! Every usable adapter can be selected by its PCI device id.
  NOLINT_TEST(DeviceManagerSelectionTest, DeviceIdHintSelectsEachAdapter)
  {
    const auto usable = DiscoverUsableAdapters();
    ASSERT_FALSE(usable.empty());

    for (const auto& adapter : usable) {
      SCOPED_TRACE(adapter.name);
      auto desc = MakeDesc();
      desc.preferred_card_device_id = static_cast<DeviceId>(adapter.device_id);

      const auto manager = DeviceManager(desc);

      const auto selected = SelectedAdapter(manager);
      ASSERT_HAS_VALUE(selected);
      EXPECT_EQ(selected->device_id, adapter.device_id);
    }
  }

  //! Every usable adapter can be selected by its name.
  NOLINT_TEST(DeviceManagerSelectionTest, NameHintSelectsEachAdapter)
  {
    const auto usable = DiscoverUsableAdapters();
    ASSERT_FALSE(usable.empty());

    for (const auto& adapter : usable) {
      SCOPED_TRACE(adapter.name);
      auto desc = MakeDesc();
      desc.preferred_card_name = adapter.name;

      const auto manager = DeviceManager(desc);

      const auto selected = SelectedAdapter(manager);
      ASSERT_HAS_VALUE(selected);
      EXPECT_TRUE(selected->name.contains(adapter.name));
    }
  }

  //! The module lists every adapter without a backend instance, and its
  //! default choice is the adapter a device manager selects without hints.
  NOLINT_TEST(DeviceManagerSelectionTest, ModuleEnumerationMatchesSelection)
  {
    const auto* api
      = static_cast<const GraphicsModuleApi*>(GetGraphicsModuleApi());
    ASSERT_NE(api, nullptr);
    auto listed = std::vector<GraphicsAdapterRecord> {};
    auto names = std::vector<std::string> {};
    struct Sink {
      std::vector<std::string>* names;
      std::vector<GraphicsAdapterRecord>* records;
    } sink { .names = &names, .records = &listed };

    ASSERT_TRUE(api->EnumerateAdapters(
      &sink, [](void* context, const GraphicsAdapterRecord& record) -> void {
        auto* target = static_cast<Sink*>(context);
        target->names->emplace_back(record.name);
        auto copy = record;
        copy.name = nullptr;
        copy.feature_level = nullptr;
        target->records->push_back(copy);
      }));

    const auto usable = DiscoverUsableAdapters();
    ASSERT_FALSE(usable.empty());
    const auto manager = DeviceManager(MakeDesc());
    const auto selected = SelectedAdapter(manager);
    ASSERT_HAS_VALUE(selected);

    auto defaults = 0;
    for (std::size_t index = 0U; index < listed.size(); ++index) {
      if (listed[index].default_choice) {
        ++defaults;
        EXPECT_EQ(names[index], selected->name);
        EXPECT_EQ(listed[index].device_id, selected->device_id);
      }
    }
    EXPECT_EQ(defaults, 1);
  }

} // namespace
} // namespace oxygen::graphics::d3d12
