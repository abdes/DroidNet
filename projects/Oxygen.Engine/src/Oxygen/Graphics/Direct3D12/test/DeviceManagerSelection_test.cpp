//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceFeatures.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>
#include <Oxygen/Testing/GTest.h>

extern "C" auto GetGraphicsModuleApi() -> void*;

namespace oxygen::graphics::d3d12 {
namespace {

  using ::testing::Contains;
  using ::testing::ElementsAreArray;
  using ::testing::Field;
  using ::testing::IsEmpty;
  using ::testing::IsTrue;
  using ::testing::Not;
  using ::testing::Property;

  //! What a listing says about one adapter, comparable across the module
  //! boundary.
  struct ListedAdapter {
    std::string name;
    std::uint32_t device_id { 0U };
    bool qualifies { false };
    bool default_choice { false };

    auto operator==(const ListedAdapter&) const -> bool = default;
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

  auto ToListed(const AdapterReport& report) -> ListedAdapter
  {
    return ListedAdapter {
      .name = report.info.Name(),
      .device_id = report.info.DeviceId(),
      .qualifies = report.shortfalls.empty(),
      .default_choice = report.info.IsBest(),
    };
  }

  //! The adapters on this machine that meet every requirement.
  auto QualifyingAdapters() -> std::vector<AdapterInfo>
  {
    return DeviceManager::ListAdapters(MakeDesc())
      | std::views::filter(
        [](const AdapterReport& report) { return report.shortfalls.empty(); })
      | std::views::transform(&AdapterReport::info)
      | std::ranges::to<std::vector>();
  }

  //! The adapter `manager` selected; none when it has no device.
  auto SelectedAdapter(const DeviceManager& manager)
    -> std::optional<AdapterInfo>
  {
    if (manager.Device() == nullptr) {
      return std::nullopt;
    }
    for (const auto& adapter : manager.Adapters()) {
      if (adapter.IsBest()) {
        return adapter;
      }
    }
    return std::nullopt;
  }

  //! A device manager built from `desc` selects `expected`, and the selected
  //! device meets the requirements.
  auto ExpectSelects(const DeviceManagerDesc& desc, const AdapterInfo& expected)
    -> void
  {
    const auto manager = DeviceManager(desc);

    const auto selected = SelectedAdapter(manager);
    ASSERT_HAS_VALUE(selected);
    EXPECT_EQ(selected->DeviceId(), expected.DeviceId());
    EXPECT_TRUE(selected->Name().contains(expected.Name()));
    EXPECT_THAT(
      MissingRequirements(manager.Features(), desc.requirements), IsEmpty());
  }

  //! The listing marks one default adapter, and it is the one a device manager
  //! selects without hints.
  NOLINT_TEST(DeviceManagerSelectionTest, ListedDefaultIsTheSelectedAdapter)
  {
    const auto reports = DeviceManager::ListAdapters(MakeDesc());

    EXPECT_THAT(reports,
      Contains(
        Field(&AdapterReport::info, Property(&AdapterInfo::IsBest, IsTrue())))
        .Times(1));
    const auto best = std::ranges::find_if(reports,
      [](const AdapterReport& report) { return report.info.IsBest(); });
    ASSERT_NE(best, reports.end());
    EXPECT_THAT(best->shortfalls, IsEmpty());
    GCHECK_F(ExpectSelects(MakeDesc(), best->info));
  }

  //! Every qualifying adapter can be selected by its PCI device id.
  NOLINT_TEST(DeviceManagerSelectionTest, DeviceIdHintSelectsEachAdapter)
  {
    const auto adapters = QualifyingAdapters();
    ASSERT_THAT(adapters, Not(IsEmpty()));

    for (const auto& adapter : adapters) {
      auto desc = MakeDesc();
      desc.preferred_card_device_id = static_cast<DeviceId>(adapter.DeviceId());
      TRACE_GCHECK_F(ExpectSelects(desc, adapter), adapter.Name());
    }
  }

  //! Every qualifying adapter can be selected by its name.
  NOLINT_TEST(DeviceManagerSelectionTest, NameHintSelectsEachAdapter)
  {
    const auto adapters = QualifyingAdapters();
    ASSERT_THAT(adapters, Not(IsEmpty()));

    for (const auto& adapter : adapters) {
      auto desc = MakeDesc();
      desc.preferred_card_name = adapter.Name();
      TRACE_GCHECK_F(ExpectSelects(desc, adapter), adapter.Name());
    }
  }

  //! The backend module lists the same adapters, requirement results and
  //! default choice as the device manager.
  NOLINT_TEST(DeviceManagerSelectionTest, ModuleEnumerationMatchesListing)
  {
    const auto* api
      = static_cast<const GraphicsModuleApi*>(GetGraphicsModuleApi());
    ASSERT_NE(api, nullptr);
    auto listed = std::vector<ListedAdapter> {};

    ASSERT_TRUE(api->EnumerateAdapters(
      &listed, [](void* context, const GraphicsAdapterRecord& record) {
        static_cast<std::vector<ListedAdapter>*>(context)->push_back({
          .name = record.name,
          .device_id = record.device_id,
          .qualifies = record.missing_requirement_count == 0U,
          .default_choice = record.default_choice,
        });
      }));

    EXPECT_THAT(listed,
      ElementsAreArray(DeviceManager::ListAdapters(MakeDesc())
        | std::views::transform(ToListed) | std::ranges::to<std::vector>()));
  }

} // namespace
} // namespace oxygen::graphics::d3d12
