//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <d3d12.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/MemoryStatistics.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>

namespace oxygen::vortex::testing {
namespace {
  NOLINT_TEST_F(
    LightingGpuAbiTest, BudgetChargesNativeSizeThroughDeferredRetirement)
  {
    Backend().BeginFrame(frame::SequenceNumber { 1U }, frame::Slot { 0U });
    auto description = D3D12_RESOURCE_DESC {};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = 4097U;
    description.Height = 1U;
    description.DepthOrArraySize = 1U;
    description.MipLevels = 1U;
    description.SampleDesc.Count = 1U;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto required = Backend()
                            .GetCurrentDevice()
                            ->GetResourceAllocationInfo(0U, 1U, &description)
                            .SizeInBytes;
    ASSERT_GT(required, description.Width);
    auto budget = std::make_shared<graphics::AllocationBudget>(
      graphics::AllocationBudgetLimits {
        .total = SizeBytes { 2U * required },
        .compact_indices = SizeBytes { required },
        .driver_headroom = SizeBytes { 0U },
      });
    const auto desc = graphics::BufferDesc {
      .size_bytes = description.Width,
      .allocation_budget = { .owner = budget },
    };
    auto first = Backend().CreateBuffer(desc);
    auto second = Backend().CreateBuffer(desc);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(budget->Snapshot().allocated.get(), 2U * required);
    EXPECT_THROW(
      (void)Backend().CreateBuffer(desc), graphics::AllocationBudgetExceeded);
    EXPECT_EQ(budget->Snapshot().last_requested.get(), required);
    EXPECT_EQ(budget->Snapshot().last_available.get(), 0U);
    Backend().RegisterDeferredRelease(std::move(first));
    EXPECT_EQ(budget->Snapshot().allocated.get(), 2U * required);
    Backend().EndFrame(frame::SequenceNumber { 1U }, frame::Slot { 0U });
    for (std::uint32_t index = 1U; index < frame::kFramesInFlight.get();
      ++index) {
      const auto sequence = frame::SequenceNumber { index + 1U };
      Backend().BeginFrame(sequence, frame::Slot { index });
      Backend().EndFrame(sequence, frame::Slot { index });
    }
    const auto reuse
      = frame::SequenceNumber { frame::kFramesInFlight.get() + 1U };
    Backend().BeginFrame(reuse, frame::Slot { 0U });
    EXPECT_EQ(budget->Snapshot().allocated.get(), required);
    auto recovered = Backend().CreateBuffer(desc);
    EXPECT_NE(recovered, nullptr);
    second.reset();
    recovered.reset();
    EXPECT_EQ(budget->Snapshot().allocated.get(), 0U);
    Backend().EndFrame(reuse, frame::Slot { 0U });
    RecordProperty("native_allocation_bytes", required);
  }

  NOLINT_TEST_F(LightingGpuAbiTest, DriverHeadroomRejectsBeforeCreatingBacking)
  {
    auto budget = std::make_shared<graphics::AllocationBudget>(
      graphics::AllocationBudgetLimits {
        .total = SizeBytes { 1024ULL * 1024ULL },
        .compact_indices = SizeBytes { 0U },
        .driver_headroom
        = SizeBytes { std::numeric_limits<std::uint64_t>::max() },
      });
    const auto before = Backend().GetMemoryStatistics();
    EXPECT_THROW((void)Backend().CreateBuffer(graphics::BufferDesc {
                   .size_bytes = 4097U,
                   .allocation_budget = { .owner = budget },
                 }),
      graphics::AllocationBudgetExceeded);
    const auto after = Backend().GetMemoryStatistics();
    EXPECT_EQ(after.local.allocation_bytes, before.local.allocation_bytes);
    EXPECT_EQ(after.local.block_bytes, before.local.block_bytes);
    EXPECT_EQ(budget->Snapshot().allocated.get(), 0U);
    EXPECT_EQ(budget->Snapshot().rejected_requests, 1U);
  }
} // namespace
} // namespace oxygen::vortex::testing
