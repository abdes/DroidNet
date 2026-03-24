//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowGpuTestFixtures.h"

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>

namespace {

using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmHzbPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::testing::VsmCacheManagerGpuTestBase;

class VsmCacheManagerGpuResourcesTest : public VsmCacheManagerGpuTestBase {
protected:
  static auto CommitFrame(VsmCacheManager& manager)
    -> const oxygen::renderer::vsm::VsmPageAllocationFrame&
  {
    static_cast<void>(manager.BuildPageAllocationPlan());
    return manager.CommitPageAllocationFrame();
  }
};

NOLINT_TEST_F(VsmCacheManagerGpuResourcesTest,
  CommitPublishesNonNullWorkingSetBuffersWithExpectedDescriptors)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase4-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase4-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  manager.BeginFrame(MakeSeam(pool_manager, 1ULL, 10U, "phase4-frame", 2U),
    VsmCacheManagerFrameConfig { .debug_name = "phase4-frame" });

  const auto& frame = CommitFrame(manager);
  ASSERT_TRUE(frame.is_ready);
  ASSERT_NE(frame.page_table_buffer, nullptr);
  ASSERT_NE(frame.page_flags_buffer, nullptr);
  ASSERT_NE(frame.dirty_flags_buffer, nullptr);
  ASSERT_NE(frame.physical_page_list_buffer, nullptr);
  ASSERT_NE(frame.page_rect_bounds_buffer, nullptr);

  const auto& page_table_desc = frame.page_table_buffer->GetDescriptor();
  EXPECT_EQ(page_table_desc.size_bytes, 2ULL * sizeof(std::uint32_t));
  EXPECT_EQ(page_table_desc.usage, BufferUsage::kStorage);
  EXPECT_EQ(page_table_desc.memory, BufferMemory::kDeviceLocal);

  const auto& page_flags_desc = frame.page_flags_buffer->GetDescriptor();
  EXPECT_EQ(page_flags_desc.size_bytes, 2ULL * sizeof(std::uint32_t));
  EXPECT_EQ(page_flags_desc.usage, BufferUsage::kStorage);

  const auto& dirty_flags_desc = frame.dirty_flags_buffer->GetDescriptor();
  EXPECT_EQ(dirty_flags_desc.size_bytes, 512ULL * sizeof(std::uint32_t));
  EXPECT_EQ(dirty_flags_desc.usage, BufferUsage::kStorage);

  const auto& page_list_desc = frame.physical_page_list_buffer->GetDescriptor();
  EXPECT_EQ(page_list_desc.size_bytes, 512ULL * sizeof(std::uint32_t));
  EXPECT_EQ(page_list_desc.usage, BufferUsage::kStorage);

  const auto& rect_bounds_desc = frame.page_rect_bounds_buffer->GetDescriptor();
  EXPECT_EQ(rect_bounds_desc.size_bytes, 2ULL * sizeof(std::uint32_t) * 4ULL);
  EXPECT_EQ(rect_bounds_desc.usage, BufferUsage::kStorage);
}

NOLINT_TEST_F(
  VsmCacheManagerGpuResourcesTest, CompatibleFramesReuseWorkingSetBuffers)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase4-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase4-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  manager.BeginFrame(MakeSeam(pool_manager, 1ULL, 10U, "phase4-frame-a", 2U),
    VsmCacheManagerFrameConfig { .debug_name = "phase4-frame-a" });
  const auto& first = CommitFrame(manager);
  const auto first_page_table = first.page_table_buffer;
  const auto first_page_flags = first.page_flags_buffer;
  const auto first_dirty_flags = first.dirty_flags_buffer;
  const auto first_page_list = first.physical_page_list_buffer;
  const auto first_rect_bounds = first.page_rect_bounds_buffer;
  manager.ExtractFrameData();

  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "phase4-frame-b", 2U),
    VsmCacheManagerFrameConfig { .debug_name = "phase4-frame-b" });
  const auto& second = CommitFrame(manager);

  EXPECT_EQ(second.page_table_buffer, first_page_table);
  EXPECT_EQ(second.page_flags_buffer, first_page_flags);
  EXPECT_EQ(second.dirty_flags_buffer, first_dirty_flags);
  EXPECT_EQ(second.physical_page_list_buffer, first_page_list);
  EXPECT_EQ(second.page_rect_bounds_buffer, first_rect_bounds);
}

NOLINT_TEST_F(VsmCacheManagerGpuResourcesTest,
  IncompatibleShapeChangesRecreateWorkingSetBuffers)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase4-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase4-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  manager.BeginFrame(MakeSeam(pool_manager, 1ULL, 10U, "phase4-frame-a", 1U),
    VsmCacheManagerFrameConfig { .debug_name = "phase4-frame-a" });
  const auto& first = CommitFrame(manager);
  const auto first_page_table = first.page_table_buffer;
  const auto first_page_flags = first.page_flags_buffer;
  const auto first_rect_bounds = first.page_rect_bounds_buffer;
  manager.ExtractFrameData();

  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "phase4-frame-b", 3U),
    VsmCacheManagerFrameConfig { .debug_name = "phase4-frame-b" });
  const auto& second = CommitFrame(manager);

  EXPECT_NE(second.page_table_buffer, first_page_table);
  EXPECT_NE(second.page_flags_buffer, first_page_flags);
  EXPECT_NE(second.page_rect_bounds_buffer, first_rect_bounds);
  EXPECT_EQ(second.page_table_buffer->GetDescriptor().size_bytes,
    3ULL * sizeof(std::uint32_t));
  EXPECT_EQ(second.page_rect_bounds_buffer->GetDescriptor().size_bytes,
    3ULL * sizeof(std::uint32_t) * 4ULL);
}

NOLINT_TEST_F(
  VsmCacheManagerGpuResourcesTest, ResetDropsReusableWorkingSetBuffers)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase4-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase4-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  manager.BeginFrame(MakeSeam(pool_manager, 1ULL, 10U, "phase4-frame-a", 2U),
    VsmCacheManagerFrameConfig { .debug_name = "phase4-frame-a" });
  const auto& first = CommitFrame(manager);
  const auto first_page_table = first.page_table_buffer;
  ASSERT_NE(first_page_table, nullptr);

  manager.Reset();
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);

  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "phase4-frame-b", 2U),
    VsmCacheManagerFrameConfig { .debug_name = "phase4-frame-b" });
  const auto& second = CommitFrame(manager);

  EXPECT_NE(second.page_table_buffer, first_page_table);
}

NOLINT_TEST_F(
  VsmCacheManagerGpuResourcesTest, CpuMirrorsRemainAvailableWhenGraphicsIsNull)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase4-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase4-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, 1ULL, 10U, "phase4-frame", 2U),
    VsmCacheManagerFrameConfig { .debug_name = "phase4-frame" });

  const auto& frame = CommitFrame(manager);
  EXPECT_EQ(frame.page_table_buffer, nullptr);
  EXPECT_EQ(frame.page_flags_buffer, nullptr);
  EXPECT_EQ(frame.dirty_flags_buffer, nullptr);
  EXPECT_EQ(frame.physical_page_list_buffer, nullptr);
  EXPECT_EQ(frame.page_rect_bounds_buffer, nullptr);
  EXPECT_EQ(frame.snapshot.page_table.size(), 2U);
  EXPECT_EQ(frame.snapshot.physical_pages.size(), 512U);
}

} // namespace
