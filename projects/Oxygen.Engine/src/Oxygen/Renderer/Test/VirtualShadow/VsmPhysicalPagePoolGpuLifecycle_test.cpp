//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowGpuTestFixtures.h"

#include <stdexcept>

#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>

namespace {

using oxygen::TextureType;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::renderer::vsm::VsmHzbPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::testing::VirtualShadowGpuTest;

class VsmPhysicalPagePoolGpuLifecycleTest : public VirtualShadowGpuTest { };

NOLINT_TEST_F(VsmPhysicalPagePoolGpuLifecycleTest,
  CreatingPoolsPublishesNonNullHandlesAndExpectedDescriptors)
{
  auto manager = VsmPhysicalPagePoolManager(&Backend());
  const auto shadow_config = MakeShadowPoolConfig();
  const auto hzb_config = MakeHzbPoolConfig();

  EXPECT_EQ(manager.EnsureShadowPool(shadow_config),
    VsmPhysicalPoolChangeResult::kCreated);
  EXPECT_EQ(
    manager.EnsureHzbPool(hzb_config), VsmHzbPoolChangeResult::kCreated);

  const auto shadow_snapshot = manager.GetShadowPoolSnapshot();
  const auto hzb_snapshot = manager.GetHzbPoolSnapshot();

  ASSERT_TRUE(shadow_snapshot.is_available);
  ASSERT_NE(shadow_snapshot.shadow_texture, nullptr);
  ASSERT_NE(shadow_snapshot.metadata_buffer, nullptr);
  ASSERT_TRUE(hzb_snapshot.is_available);
  ASSERT_NE(hzb_snapshot.texture, nullptr);

  const auto& shadow_desc = shadow_snapshot.shadow_texture->GetDescriptor();
  EXPECT_EQ(shadow_desc.width, 2048U);
  EXPECT_EQ(shadow_desc.height, 2048U);
  EXPECT_EQ(shadow_desc.array_size, shadow_config.array_slice_count);
  EXPECT_EQ(shadow_desc.format, shadow_config.depth_format);
  EXPECT_EQ(shadow_desc.texture_type, TextureType::kTexture2DArray);
  EXPECT_TRUE(shadow_desc.is_render_target);
  EXPECT_TRUE(shadow_desc.is_shader_resource);

  const auto metadata_desc = shadow_snapshot.metadata_buffer->GetDescriptor();
  EXPECT_EQ(metadata_desc.size_bytes, 2048ULL);
  EXPECT_EQ(metadata_desc.usage, BufferUsage::kStorage);
  EXPECT_EQ(metadata_desc.memory, BufferMemory::kDeviceLocal);

  const auto& hzb_desc = hzb_snapshot.texture->GetDescriptor();
  EXPECT_EQ(hzb_desc.width, 1024U);
  EXPECT_EQ(hzb_desc.height, 1024U);
  EXPECT_EQ(hzb_desc.array_size, 1U);
  EXPECT_EQ(hzb_desc.mip_levels, hzb_config.mip_count);
  EXPECT_EQ(hzb_desc.format, hzb_config.format);
  EXPECT_EQ(hzb_desc.texture_type, TextureType::kTexture2D);
  EXPECT_TRUE(hzb_desc.is_shader_resource);
  EXPECT_TRUE(hzb_desc.is_uav);
}

NOLINT_TEST_F(VsmPhysicalPagePoolGpuLifecycleTest,
  CompatibleShadowEnsurePreservesExistingHandles)
{
  auto manager = VsmPhysicalPagePoolManager(&Backend());
  const auto config = MakeShadowPoolConfig();

  ASSERT_EQ(
    manager.EnsureShadowPool(config), VsmPhysicalPoolChangeResult::kCreated);

  const auto before = manager.GetShadowPoolSnapshot();
  ASSERT_NE(before.shadow_texture, nullptr);
  ASSERT_NE(before.metadata_buffer, nullptr);

  EXPECT_EQ(
    manager.EnsureShadowPool(config), VsmPhysicalPoolChangeResult::kUnchanged);

  const auto after = manager.GetShadowPoolSnapshot();
  EXPECT_EQ(after.pool_identity, before.pool_identity);
  EXPECT_EQ(after.shadow_texture, before.shadow_texture);
  EXPECT_EQ(after.metadata_buffer, before.metadata_buffer);
}

NOLINT_TEST_F(VsmPhysicalPagePoolGpuLifecycleTest,
  IncompatibleShadowEnsureReplacesHandlesAndAdvancesIdentity)
{
  auto manager = VsmPhysicalPagePoolManager(&Backend());
  auto config = MakeShadowPoolConfig();

  ASSERT_EQ(
    manager.EnsureShadowPool(config), VsmPhysicalPoolChangeResult::kCreated);

  const auto before = manager.GetShadowPoolSnapshot();
  config.page_size_texels = 256;

  EXPECT_EQ(
    manager.EnsureShadowPool(config), VsmPhysicalPoolChangeResult::kRecreated);

  const auto after = manager.GetShadowPoolSnapshot();
  EXPECT_NE(after.pool_identity, before.pool_identity);
  EXPECT_NE(after.shadow_texture, before.shadow_texture);
  EXPECT_NE(after.metadata_buffer, before.metadata_buffer);
  EXPECT_EQ(after.shadow_texture->GetDescriptor().width, 4096U);
  EXPECT_EQ(after.shadow_texture->GetDescriptor().height, 4096U);
}

NOLINT_TEST_F(VsmPhysicalPagePoolGpuLifecycleTest,
  HzbRecreationDoesNotPerturbShadowPoolHandles)
{
  auto manager = VsmPhysicalPagePoolManager(&Backend());
  const auto shadow_config = MakeShadowPoolConfig();
  auto hzb_config = MakeHzbPoolConfig();

  ASSERT_EQ(manager.EnsureShadowPool(shadow_config),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    manager.EnsureHzbPool(hzb_config), VsmHzbPoolChangeResult::kCreated);

  const auto shadow_before = manager.GetShadowPoolSnapshot();
  const auto hzb_before = manager.GetHzbPoolSnapshot();

  hzb_config.mip_count += 1;
  EXPECT_EQ(
    manager.EnsureHzbPool(hzb_config), VsmHzbPoolChangeResult::kRecreated);

  const auto shadow_after = manager.GetShadowPoolSnapshot();
  const auto hzb_after = manager.GetHzbPoolSnapshot();
  EXPECT_EQ(shadow_after.pool_identity, shadow_before.pool_identity);
  EXPECT_EQ(shadow_after.shadow_texture, shadow_before.shadow_texture);
  EXPECT_EQ(shadow_after.metadata_buffer, shadow_before.metadata_buffer);
  EXPECT_NE(hzb_after.texture, hzb_before.texture);
  EXPECT_EQ(hzb_after.mip_count, hzb_config.mip_count);
}

NOLINT_TEST_F(VsmPhysicalPagePoolGpuLifecycleTest,
  ShadowPoolRecreationInvalidatesDerivedHzbPool)
{
  auto manager = VsmPhysicalPagePoolManager(&Backend());
  auto shadow_config = MakeShadowPoolConfig();

  ASSERT_EQ(manager.EnsureShadowPool(shadow_config),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(manager.EnsureHzbPool(MakeHzbPoolConfig()),
    VsmHzbPoolChangeResult::kCreated);

  shadow_config.page_size_texels = 256;
  ASSERT_EQ(manager.EnsureShadowPool(shadow_config),
    VsmPhysicalPoolChangeResult::kRecreated);

  const auto hzb_snapshot = manager.GetHzbPoolSnapshot();
  EXPECT_FALSE(hzb_snapshot.is_available);
  EXPECT_EQ(hzb_snapshot.texture, nullptr);
}

NOLINT_TEST_F(VsmPhysicalPagePoolGpuLifecycleTest,
  CompatibleHzbEnsurePreservesExistingHandle)
{
  auto manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(manager.EnsureShadowPool(MakeShadowPoolConfig()),
    VsmPhysicalPoolChangeResult::kCreated);
  const auto config = MakeHzbPoolConfig();

  ASSERT_EQ(manager.EnsureHzbPool(config), VsmHzbPoolChangeResult::kCreated);
  const auto before = manager.GetHzbPoolSnapshot();
  ASSERT_NE(before.texture, nullptr);

  EXPECT_EQ(manager.EnsureHzbPool(config), VsmHzbPoolChangeResult::kUnchanged);

  const auto after = manager.GetHzbPoolSnapshot();
  EXPECT_EQ(after.texture, before.texture);
  EXPECT_EQ(after.width, before.width);
  EXPECT_EQ(after.height, before.height);
  EXPECT_EQ(after.array_size, 1U);
}

NOLINT_TEST_F(VsmPhysicalPagePoolGpuLifecycleTest,
  InvalidConfigsThrowBeforePublishingGpuHandles)
{
  auto manager = VsmPhysicalPagePoolManager(&Backend());
  auto invalid_shadow = MakeShadowPoolConfig();
  invalid_shadow.page_size_texels = 0;

  EXPECT_THROW(static_cast<void>(manager.EnsureShadowPool(invalid_shadow)),
    std::invalid_argument);
  EXPECT_FALSE(manager.GetShadowPoolSnapshot().is_available);

  auto invalid_hzb = MakeHzbPoolConfig();
  EXPECT_THROW(
    static_cast<void>(manager.EnsureHzbPool(invalid_hzb)), std::logic_error);
  EXPECT_FALSE(manager.GetHzbPoolSnapshot().is_available);
}

NOLINT_TEST_F(
  VsmPhysicalPagePoolGpuLifecycleTest, ResetReleasesAllPublishedHandles)
{
  auto manager = VsmPhysicalPagePoolManager(&Backend());

  ASSERT_EQ(manager.EnsureShadowPool(MakeShadowPoolConfig()),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(manager.EnsureHzbPool(MakeHzbPoolConfig()),
    VsmHzbPoolChangeResult::kCreated);

  manager.Reset();

  const auto shadow_snapshot = manager.GetShadowPoolSnapshot();
  const auto hzb_snapshot = manager.GetHzbPoolSnapshot();
  EXPECT_FALSE(shadow_snapshot.is_available);
  EXPECT_FALSE(hzb_snapshot.is_available);
  EXPECT_EQ(shadow_snapshot.shadow_texture, nullptr);
  EXPECT_EQ(shadow_snapshot.metadata_buffer, nullptr);
  EXPECT_EQ(hzb_snapshot.texture, nullptr);
}

} // namespace
