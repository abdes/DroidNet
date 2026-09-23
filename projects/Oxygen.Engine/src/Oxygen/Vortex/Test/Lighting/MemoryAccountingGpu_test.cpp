//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <d3d12.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/MemoryStatistics.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/OffscreenTestFixture.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Support/D3D12MemoryCapture.h>

namespace oxygen::vortex::testing {
namespace {
  class MemoryAccountingGpuTest
    : public graphics::d3d12::testing::OffscreenTestFixture {
  protected:
    auto BackendConfigJson() const -> std::string override
    {
      return R"({"enable_debug_layer":true})";
    }
  };

  NOLINT_TEST_F(
    MemoryAccountingGpuTest, CountsKnownAllocationsThroughDeferredRetirement)
  {
    auto capture = D3D12MemoryCapture(MemorySampleCapacity { 5U });
    const auto allocations = [](const auto& statistics) -> std::uint64_t {
      return static_cast<std::uint64_t>(statistics.local.allocation_count)
        + statistics.non_local.allocation_count;
    };
    const auto bytes = [](const auto& statistics) -> std::uint64_t {
      return statistics.local.allocation_bytes.get()
        + statistics.non_local.allocation_bytes.get();
    };
    Backend().BeginFrame(frame::SequenceNumber { 1U }, frame::Slot { 0U });
    const auto before = Backend().GetMemoryStatistics();
    ASSERT_TRUE(capture.Record(
      MemorySampleId { 0U }, frame::SequenceNumber { 1U }, before));
    // Use backend factories directly: the fixture must not retain extra owners.
    auto buffer = Backend().CreateBuffer({
      .size_bytes = 1U << 20U,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "Memory accounting known buffer",
    });
    auto texture = Backend().CreateTexture({
      .width = 128U,
      .height = 128U,
      .format = Format::kRGBA16Float,
      .debug_name = "Memory accounting known texture",
    });
    auto upload = Backend().CreateBuffer({
      .size_bytes = 1U << 22U,
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = "Memory accounting known upload buffer",
    });
    ASSERT_NE(buffer, nullptr);
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(upload, nullptr);
    const auto footprint = [&](const auto& resource) -> std::uint64_t {
      const auto shape = resource->GetNativeResource()
                           ->template AsPointer<ID3D12Resource>()
                           ->GetDesc();
      return Backend()
        .GetCurrentDevice()
        ->GetResourceAllocationInfo(0U, 1U, &shape)
        .SizeInBytes;
    };
    const auto device_bytes = footprint(buffer) + footprint(texture);
    const auto upload_bytes = footprint(upload);
    const auto expected_bytes = device_bytes + upload_bytes;
    const auto live = Backend().GetMemoryStatistics();
    EXPECT_EQ(allocations(live), allocations(before) + 3U);
    EXPECT_EQ(bytes(live), bytes(before) + expected_bytes);
    auto architecture = D3D12_FEATURE_DATA_ARCHITECTURE {};
    ASSERT_EQ(
      Backend().GetCurrentDevice()->CheckFeatureSupport(
        D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture)),
      0);
    const bool unified_memory = architecture.UMA != 0;
    EXPECT_EQ(
      live.local.allocation_bytes.get() - before.local.allocation_bytes.get(),
      unified_memory ? expected_bytes : device_bytes);
    EXPECT_EQ(live.non_local.allocation_bytes.get()
        - before.non_local.allocation_bytes.get(),
      unified_memory ? 0U : upload_bytes);
    ASSERT_TRUE(capture.Record(
      MemorySampleId { 1U }, frame::SequenceNumber { 1U }, live));
    const auto buffer_lifetime = std::weak_ptr(buffer);
    const auto texture_lifetime = std::weak_ptr(texture);
    const auto upload_lifetime = std::weak_ptr(upload);
    GetGraphicsShared()->RegisterDeferredRelease(std::move(buffer));
    GetGraphicsShared()->RegisterDeferredRelease(std::move(texture));
    GetGraphicsShared()->RegisterDeferredRelease(std::move(upload));
    const auto pending = Backend().GetMemoryStatistics();
    EXPECT_FALSE(buffer_lifetime.expired());
    EXPECT_FALSE(texture_lifetime.expired());
    EXPECT_FALSE(upload_lifetime.expired());
    EXPECT_EQ(bytes(pending), bytes(live));
    EXPECT_EQ(allocations(pending), allocations(live));
    ASSERT_TRUE(capture.Record(
      MemorySampleId { 2U }, frame::SequenceNumber { 1U }, pending));
    Backend().EndFrame(frame::SequenceNumber { 1U }, frame::Slot { 0U });
    for (std::uint32_t index = 1U; index < frame::kFramesInFlight.get();
      ++index) {
      const auto sequence = frame::SequenceNumber { index + 1U };
      Backend().BeginFrame(sequence, frame::Slot { index });
      EXPECT_FALSE(buffer_lifetime.expired());
      EXPECT_FALSE(texture_lifetime.expired());
      EXPECT_FALSE(upload_lifetime.expired());
      EXPECT_EQ(bytes(Backend().GetMemoryStatistics()), bytes(live));
      Backend().EndFrame(sequence, frame::Slot { index });
    }
    const auto held = Backend().GetMemoryStatistics();
    ASSERT_TRUE(capture.Record(MemorySampleId { 3U },
      frame::SequenceNumber { frame::kFramesInFlight.get() }, held));
    const auto retired_frame
      = frame::SequenceNumber { frame::kFramesInFlight.get() + 1U };
    Backend().BeginFrame(retired_frame, frame::Slot { 0U });
    const auto retired = Backend().GetMemoryStatistics();
    EXPECT_TRUE(buffer_lifetime.expired());
    EXPECT_TRUE(texture_lifetime.expired());
    EXPECT_TRUE(upload_lifetime.expired());
    EXPECT_EQ(allocations(retired), allocations(before));
    EXPECT_EQ(bytes(retired), bytes(before));
    ASSERT_TRUE(capture.Record(MemorySampleId { 4U }, retired_frame, retired));
    Backend().EndFrame(retired_frame, frame::Slot { 0U });
    const auto report = capture.Report();
    EXPECT_EQ(report.at("samples").size(), 5U);
    RecordProperty("known_allocation_bytes", expected_bytes);
    RecordProperty("known_device_bytes", device_bytes);
    RecordProperty("known_upload_bytes", upload_bytes);
    RecordProperty("unified_memory", unified_memory);
    RecordProperty("memory_samples", report.dump());
  }
} // namespace
} // namespace oxygen::vortex::testing
