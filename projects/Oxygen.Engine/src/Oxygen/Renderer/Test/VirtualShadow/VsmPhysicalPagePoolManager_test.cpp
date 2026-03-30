//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::Format;
using oxygen::renderer::vsm::IsValid;
using oxygen::renderer::vsm::to_string;
using oxygen::renderer::vsm::Validate;
using oxygen::renderer::vsm::VsmHzbPoolConfig;
using oxygen::renderer::vsm::VsmHzbPoolConfigValidationResult;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPoolCompatibilityResult;
using oxygen::renderer::vsm::VsmPhysicalPoolConfig;
using oxygen::renderer::vsm::VsmPhysicalPoolConfigValidationResult;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;
using oxygen::renderer::vsm::testing::VsmPhysicalPoolTestBase;

class VsmPhysicalPagePoolManagerTest : public VsmPhysicalPoolTestBase { };

NOLINT_TEST_F(VsmPhysicalPagePoolManagerTest, PhysicalPagePoolManagerConstructs)
{
  const auto manager = VsmPhysicalPagePoolManager(nullptr);
  EXPECT_FALSE(manager.IsShadowPoolAvailable());
  EXPECT_FALSE(manager.IsHzbPoolAvailable());
  EXPECT_EQ(manager.GetPoolIdentity(), 0ULL);
}

NOLINT_TEST_F(
  VsmPhysicalPagePoolManagerTest, PhysicalPoolEnsureProducesOwningSnapshot)
{
  auto manager = VsmPhysicalPagePoolManager(nullptr);

  VsmPhysicalPoolConfig dynamic_only {};
  dynamic_only.page_size_texels = 128;
  dynamic_only.physical_tile_capacity = 256;
  dynamic_only.array_slice_count = 1;
  dynamic_only.depth_format = Format::kDepth32;
  dynamic_only.slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth };
  dynamic_only.debug_name = "phase0-shadow-pool";

  EXPECT_EQ(manager.EnsureShadowPool(dynamic_only),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto snapshot = manager.GetShadowPoolSnapshot();
  EXPECT_TRUE(snapshot.is_available);
  EXPECT_EQ(snapshot.slice_count, 1U);
  EXPECT_EQ(snapshot.tiles_per_axis, 16U);
  EXPECT_EQ(snapshot.shadow_texture, nullptr);
  EXPECT_EQ(snapshot.metadata_buffer, nullptr);
  ASSERT_EQ(snapshot.slice_roles.size(), 1U);
  EXPECT_EQ(snapshot.slice_roles[0], VsmPhysicalPoolSliceRole::kDynamicDepth);

  auto static_enabled = dynamic_only;
  static_enabled.physical_tile_capacity = 512;
  static_enabled.array_slice_count = 2;
  static_enabled.slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth,
    VsmPhysicalPoolSliceRole::kStaticDepth };

  EXPECT_EQ(manager.EnsureShadowPool(static_enabled),
    VsmPhysicalPoolChangeResult::kRecreated);
  ASSERT_EQ(snapshot.slice_roles.size(), 1U);
  EXPECT_EQ(snapshot.slice_roles[0], VsmPhysicalPoolSliceRole::kDynamicDepth);
  EXPECT_STREQ(to_string(VsmPhysicalPoolChangeResult::kRecreated), "Recreated");
}

NOLINT_TEST_F(VsmPhysicalPagePoolManagerTest,
  PhysicalPoolManagerReportsCompatibilityAndIdentityTransitions)
{
  auto manager = VsmPhysicalPagePoolManager(nullptr);

  auto config = VsmPhysicalPoolConfig {
    .page_size_texels = 128,
    .physical_tile_capacity = 512,
    .array_slice_count = 2,
    .depth_format = Format::kDepth32,
    .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth,
      VsmPhysicalPoolSliceRole::kStaticDepth },
    .debug_name = "phase2-shadow-pool",
  };

  EXPECT_EQ(manager.ComputeCompatibility(config),
    VsmPhysicalPoolCompatibilityResult::kUnavailable);
  EXPECT_EQ(
    manager.EnsureShadowPool(config), VsmPhysicalPoolChangeResult::kCreated);

  const auto created_identity = manager.GetPoolIdentity();
  EXPECT_EQ(manager.ComputeCompatibility(config),
    VsmPhysicalPoolCompatibilityResult::kCompatible);
  EXPECT_EQ(
    manager.EnsureShadowPool(config), VsmPhysicalPoolChangeResult::kUnchanged);
  EXPECT_EQ(manager.GetPoolIdentity(), created_identity);

  auto format_changed = config;
  format_changed.depth_format = Format::kDepth32Stencil8;
  EXPECT_EQ(manager.ComputeCompatibility(format_changed),
    VsmPhysicalPoolCompatibilityResult::kDepthFormatMismatch);
  EXPECT_EQ(manager.EnsureShadowPool(format_changed),
    VsmPhysicalPoolChangeResult::kRecreated);
  EXPECT_NE(manager.GetPoolIdentity(), created_identity);

  manager.Reset();
  EXPECT_FALSE(manager.IsShadowPoolAvailable());
  EXPECT_EQ(manager.GetPoolIdentity(), 0ULL);
}

NOLINT_TEST_F(VsmPhysicalPagePoolManagerTest,
  PhysicalPoolManagerRejectsInvalidEnsureRequestsEarly)
{
  auto manager = VsmPhysicalPagePoolManager(nullptr);

  const auto invalid_shadow_config = VsmPhysicalPoolConfig {
    .page_size_texels = 0,
    .physical_tile_capacity = 256,
    .array_slice_count = 1,
    .depth_format = Format::kDepth32,
    .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth },
    .debug_name = "invalid-shadow",
  };
  EXPECT_EQ(manager.ComputeCompatibility(invalid_shadow_config),
    VsmPhysicalPoolCompatibilityResult::kInvalidRequestedConfig);
  EXPECT_THROW(
    static_cast<void>(manager.EnsureShadowPool(invalid_shadow_config)),
    std::invalid_argument);
  EXPECT_FALSE(manager.IsShadowPoolAvailable());

  const auto invalid_tile_layout = VsmPhysicalPoolConfig {
    .page_size_texels = 128,
    .physical_tile_capacity = 257,
    .array_slice_count = 1,
    .depth_format = Format::kDepth32,
    .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth },
    .debug_name = "invalid-tile-layout",
  };
  EXPECT_THROW(static_cast<void>(manager.EnsureShadowPool(invalid_tile_layout)),
    std::invalid_argument);
  EXPECT_FALSE(manager.IsShadowPoolAvailable());

  const auto invalid_hzb_config = VsmHzbPoolConfig {
    .mip_count = 8,
    .format = Format::kR32Float,
    .debug_name = "invalid-hzb",
  };
  EXPECT_THROW(static_cast<void>(manager.EnsureHzbPool(invalid_hzb_config)),
    std::logic_error);
  EXPECT_FALSE(manager.IsHzbPoolAvailable());
}

NOLINT_TEST_F(VsmPhysicalPagePoolManagerTest,
  PhysicalPoolConfigValidationRejectsInvalidDefaultsAndSliceLayouts)
{
  const auto default_config = VsmPhysicalPoolConfig {};
  EXPECT_FALSE(IsValid(default_config));
  EXPECT_EQ(Validate(default_config),
    VsmPhysicalPoolConfigValidationResult::kZeroPageSize);

  auto missing_capacity = VsmPhysicalPoolConfig {
    .page_size_texels = 128,
    .physical_tile_capacity = 0,
    .array_slice_count = 1,
    .depth_format = Format::kDepth32,
    .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth },
    .debug_name = "missing-capacity",
  };
  EXPECT_EQ(Validate(missing_capacity),
    VsmPhysicalPoolConfigValidationResult::kZeroTileCapacity);

  auto non_square_per_slice = VsmPhysicalPoolConfig {
    .page_size_texels = 128,
    .physical_tile_capacity = 513,
    .array_slice_count = 1,
    .depth_format = Format::kDepth32,
    .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth },
    .debug_name = "non-square-per-slice",
  };
  EXPECT_EQ(Validate(non_square_per_slice),
    VsmPhysicalPoolConfigValidationResult::kNonSquarePerSliceLayout);

  auto mismatched_role_count = VsmPhysicalPoolConfig {
    .page_size_texels = 128,
    .physical_tile_capacity = 256,
    .array_slice_count = 2,
    .depth_format = Format::kDepth32,
    .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth },
    .debug_name = "mismatched-role-count",
  };
  EXPECT_EQ(Validate(mismatched_role_count),
    VsmPhysicalPoolConfigValidationResult::kSliceRoleCountMismatch);

  auto duplicate_roles = VsmPhysicalPoolConfig {
    .page_size_texels = 128,
    .physical_tile_capacity = 256,
    .array_slice_count = 2,
    .depth_format = Format::kDepth32,
    .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth,
      VsmPhysicalPoolSliceRole::kDynamicDepth },
    .debug_name = "duplicate-roles",
  };
  EXPECT_EQ(Validate(duplicate_roles),
    VsmPhysicalPoolConfigValidationResult::kDuplicateSliceRole);

  auto invalid_static_layout = VsmPhysicalPoolConfig {
    .page_size_texels = 128,
    .physical_tile_capacity = 256,
    .array_slice_count = 2,
    .depth_format = Format::kDepth32,
    .slice_roles = { VsmPhysicalPoolSliceRole::kStaticDepth,
      VsmPhysicalPoolSliceRole::kDynamicDepth },
    .debug_name = "invalid-static-layout",
  };
  EXPECT_EQ(Validate(invalid_static_layout),
    VsmPhysicalPoolConfigValidationResult::kInvalidStaticSliceLayout);

  auto valid_static_layout = VsmPhysicalPoolConfig {
    .page_size_texels = 128,
    .physical_tile_capacity = 512,
    .array_slice_count = 2,
    .depth_format = Format::kDepth32,
    .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth,
      VsmPhysicalPoolSliceRole::kStaticDepth },
    .debug_name = "valid-static-layout",
  };
  EXPECT_TRUE(IsValid(valid_static_layout));
  EXPECT_EQ(Validate(valid_static_layout),
    VsmPhysicalPoolConfigValidationResult::kValid);
  EXPECT_STREQ(
    to_string(VsmPhysicalPoolConfigValidationResult::kValid), "Valid");
}

NOLINT_TEST_F(
  VsmPhysicalPagePoolManagerTest, HzbPoolConfigValidationRejectsInvalidDefaults)
{
  const auto default_config = VsmHzbPoolConfig {};
  EXPECT_FALSE(IsValid(default_config));
  EXPECT_EQ(
    Validate(default_config), VsmHzbPoolConfigValidationResult::kZeroMipCount);

  const auto valid_config = VsmHzbPoolConfig {
    .mip_count = 10,
    .format = Format::kR32Float,
    .debug_name = "phase1-hzb-pool",
  };
  EXPECT_TRUE(IsValid(valid_config));
  EXPECT_EQ(Validate(valid_config), VsmHzbPoolConfigValidationResult::kValid);
  EXPECT_STREQ(to_string(VsmHzbPoolConfigValidationResult::kValid), "Valid");
}

NOLINT_TEST_F(VsmPhysicalPagePoolManagerTest,
  HzbPoolRequiresActiveShadowPoolAndDerivesShapeFromIt)
{
  auto manager = VsmPhysicalPagePoolManager(nullptr);

  const auto hzb_config = VsmHzbPoolConfig {
    .mip_count = 10,
    .format = Format::kR32Float,
    .debug_name = "phase1-hzb-pool",
  };
  EXPECT_THROW(
    static_cast<void>(manager.EnsureHzbPool(hzb_config)), std::logic_error);

  const auto shadow_config = VsmPhysicalPoolConfig {
    .page_size_texels = 128,
    .physical_tile_capacity = 512,
    .array_slice_count = 2,
    .depth_format = Format::kDepth32,
    .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth,
      VsmPhysicalPoolSliceRole::kStaticDepth },
    .debug_name = "phase1-shadow-pool",
  };
  ASSERT_EQ(manager.EnsureShadowPool(shadow_config),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(manager.EnsureHzbPool(hzb_config),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  const auto snapshot = manager.GetHzbPoolSnapshot();
  EXPECT_TRUE(snapshot.is_available);
  EXPECT_EQ(snapshot.width, 1024U);
  EXPECT_EQ(snapshot.height, 1024U);
  EXPECT_EQ(snapshot.array_size, 1U);
  EXPECT_EQ(snapshot.mip_count, hzb_config.mip_count);
}

} // namespace
