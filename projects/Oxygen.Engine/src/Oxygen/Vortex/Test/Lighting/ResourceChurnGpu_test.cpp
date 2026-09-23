//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/MemoryStatistics.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>

namespace oxygen::vortex::testing {
namespace {
  using ResourceChurnGpuTest = exposure::ExposureGpuTest;

  NOLINT_TEST_F(
    ResourceChurnGpuTest, CreationEventsSurviveImmediateResourceDestruction)
  {
    auto& backend = FailureBackend();
    const auto before = backend.GetMemoryStatistics();
    backend.count_resource_creations = true;
    for (unsigned index = 0U; index < 17U; ++index) {
      const auto buffer = backend.CreateBuffer({
        .size_bytes = 4096U,
        .usage = graphics::BufferUsage::kStorage,
        .memory = graphics::BufferMemory::kDeviceLocal,
        .debug_name = "Churn fixture buffer",
      });
      ASSERT_NE(buffer, nullptr);
    }
    for (unsigned index = 0U; index < 3U; ++index) {
      const auto texture = backend.CreateTexture({
        .width = 32U,
        .height = 32U,
        .format = Format::kRGBA8UNorm,
        .debug_name = "Churn fixture texture",
      });
      ASSERT_NE(texture, nullptr);
    }
    backend.count_resource_creations = false;
    const auto events = backend.resource_creations.Snapshot();
    ASSERT_TRUE(events.has_value());
    EXPECT_EQ(events->buffers, 17U);
    EXPECT_EQ(events->textures, 3U);
    EXPECT_EQ(events->requested_buffer_bytes.get(), 69632U);
    const auto after = backend.GetMemoryStatistics();
    EXPECT_EQ(
      after.local.allocation_bytes.get(), before.local.allocation_bytes.get());
    EXPECT_EQ(after.non_local.allocation_bytes.get(),
      before.non_local.allocation_bytes.get());
    const auto ignored = backend.CreateBuffer({
      .size_bytes = 4096U,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
    });
    ASSERT_NE(ignored, nullptr);
    ASSERT_TRUE(backend.resource_creations.Snapshot().has_value());
    EXPECT_EQ(backend.resource_creations.Snapshot()->buffers, 17U);
    RecordProperty("buffer_creations", events->buffers);
    RecordProperty("texture_creations", events->textures);
    RecordProperty(
      "requested_buffer_bytes", events->requested_buffer_bytes.get());
  }
} // namespace
} // namespace oxygen::vortex::testing
