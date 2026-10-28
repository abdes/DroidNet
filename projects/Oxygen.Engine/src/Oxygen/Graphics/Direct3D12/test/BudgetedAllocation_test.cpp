//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/test/Fixtures/OffscreenTestFixture.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::graphics::d3d12::testing {
namespace {

  //! The PCI device ids of this machine's adapters that meet every
  //! requirement.
  auto QualifyingDeviceIds() -> std::vector<DeviceId>
  {
    return DeviceManager::ListAdapters(DeviceManagerDesc {
             .enable_debug_layer = false,
             .enable_aftermath = false,
           })
      | std::views::filter(
        [](const AdapterReport& report) { return report.shortfalls.empty(); })
      | std::views::transform([](const AdapterReport& report) {
          return static_cast<DeviceId>(report.info.DeviceId());
        })
      | std::ranges::to<std::vector>();
  }

  //! A D3D12 backend on the adapter with the parameter's PCI device id.
  class BudgetedAllocationTest
    : public OffscreenTestFixture,
      public ::testing::WithParamInterface<DeviceId> {
  protected:
    [[nodiscard]] auto BackendConfigJson() const -> std::string override
    {
      auto config = nlohmann::json::object();
      config["preferred_card_device_id"] = GetParam();
      return config.dump();
    }
  };

  //! Buffers charged to an allocation budget are created in every memory
  //! type, including on UMA adapters, whose memory is all one local segment.
  NOLINT_TEST_P(BudgetedAllocationTest, BudgetedBuffersFitInEveryMemoryType)
  {
    constexpr auto kBufferBytes = 64ULL * 1024ULL;
    constexpr auto kMemoryTypes = std::array {
      BufferMemory::kDeviceLocal,
      BufferMemory::kUpload,
      BufferMemory::kReadBack,
    };
    const auto budget
      = std::make_shared<AllocationBudget>(AllocationBudgetLimits {
        .total = SizeBytes { 1024ULL * 1024ULL },
        .compact_indices = SizeBytes { 0U },
        .driver_headroom = SizeBytes { 0U },
      });

    for (const auto memory : kMemoryTypes) {
      const auto buffer = Backend().CreateBuffer({
        .size_bytes = kBufferBytes,
        .usage = BufferUsage::kNone,
        .memory = memory,
        .debug_name = "BudgetedAllocationTest.Buffer",
        .allocation_budget = { .owner = budget },
      });

      EXPECT_NE(buffer, nullptr);
    }
    EXPECT_EQ(budget->Snapshot().rejected_requests, 0U);
  }

  INSTANTIATE_TEST_SUITE_P(Adapters, BudgetedAllocationTest,
    ::testing::ValuesIn(QualifyingDeviceIds()),
    [](const ::testing::TestParamInfo<DeviceId>& info) {
      return fmt::format("Gpu{:04x}", info.param);
    });

} // namespace
} // namespace oxygen::graphics::d3d12::testing
