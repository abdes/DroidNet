//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: SceneAsset override validation (ComponentType::kOverride and
// ComponentType::kOverrideLodPolicy).

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/test/Fixtures/SceneAssetBuilder.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace world = data::pak::world;
namespace geometry = data::pak::geometry;
using data::ComponentType;
using data::schema::OverrideId;
using data::testing::SceneAssetBuilder;

constexpr float kLodDistance = 10.0F;
constexpr float kLodHysteresis = 0.1F;

auto Revision() -> oxygen::base::Sha256Digest
{
  auto revision = oxygen::base::Sha256Digest {};
  revision.front() = 1U;
  return revision;
}

auto Surface() -> data::SurfaceId
{
  return data::SurfaceId::FromStableIdentity("SceneOverridesTest/surface");
}

auto SurfaceFlag(const OverrideId id, const uint8_t value)
  -> world::OverrideRecord
{
  auto record = world::OverrideRecord {
    .node_index = 0U,
    .override_id = id,
    .part_id = Surface(),
    .layout_revision = Revision(),
  };
  record.value.as_bool = value;
  return record;
}

auto LodPolicyOverride(const uint32_t index) -> world::OverrideRecord
{
  auto record = world::OverrideRecord {
    .node_index = 0U,
    .override_id = OverrideId::kGeometryLodPolicy,
  };
  record.value.as_record_index = world::OverrideRecordIndex { index };
  return record;
}

auto DistancePolicy() -> geometry::LodPolicyRecord
{
  auto policy = geometry::LodPolicyRecord {
    .type = geometry::LodPolicyType::kDistance,
    .boundary_count = 1U,
  };
  policy.thresholds[0] = kLodDistance;
  policy.hysteresis_ratio = kLodHysteresis;
  return policy;
}

//! A one-node scene with a renderable and `overrides`, plus `policies` when
//! any.
auto Scene(std::vector<world::OverrideRecord> overrides,
  std::vector<geometry::LodPolicyRecord> policies = {})
  -> std::shared_ptr<data::SceneAsset>
{
  auto builder = SceneAssetBuilder {};
  builder.AddNode("Mesh");
  builder.AddComponents(ComponentType::kRenderable,
    std::vector { world::RenderableRecord {
      .node_index = 0U,
      .geometry_key = data::AssetKey::FromVirtualPath("/test/Mesh.ogeo"),
    } });
  builder.AddComponents(ComponentType::kOverride, std::move(overrides));
  if (!policies.empty()) {
    builder.AddComponents(
      ComponentType::kOverrideLodPolicy, std::move(policies));
  }
  return builder.Build();
}

//! Each value type's override is accepted with a well-formed value.
NOLINT_TEST(SceneOverridesTest, AcceptsOneOverrideOfEachValueType)
{
  auto material = world::OverrideRecord {
    .node_index = 0U,
    .override_id = OverrideId::kGeometrySurfaceMaterial,
    .part_id = Surface(),
    .layout_revision = Revision(),
  };
  material.value.as_asset
    = data::AssetKey::FromVirtualPath("/test/Material.omat");

  const auto scene = Scene(
    {
      material,
      SurfaceFlag(OverrideId::kGeometrySurfaceVisibility, 0U),
      SurfaceFlag(OverrideId::kGeometrySurfaceCastsShadows, 1U),
      LodPolicyOverride(0U),
    },
    { DistancePolicy() });

  EXPECT_EQ(scene->GetComponents<world::OverrideRecord>().size(), 4U);
  EXPECT_EQ(scene->GetComponents<geometry::LodPolicyRecord>().size(), 1U);
}

//! An id the schema does not declare is refused.
NOLINT_TEST(SceneOverridesTest, RejectsUnknownOverrideId)
{
  auto record = SurfaceFlag(OverrideId::kGeometrySurfaceVisibility, 1U);
  record.override_id = OverrideId::kInvalid;

  EXPECT_THROW(Scene({ record }), std::runtime_error);
}

//! A surface property needs its surface and layout revision; a component
//! property names neither.
NOLINT_TEST(SceneOverridesTest, RejectsPartsThatDoNotMatchTheProperty)
{
  auto no_surface = SurfaceFlag(OverrideId::kGeometrySurfaceVisibility, 1U);
  no_surface.part_id = data::SurfaceId {};
  auto no_revision = SurfaceFlag(OverrideId::kGeometrySurfaceVisibility, 1U);
  no_revision.layout_revision = {};
  auto with_surface = LodPolicyOverride(0U);
  with_surface.part_id = Surface();

  EXPECT_THROW(Scene({ no_surface }), std::runtime_error);
  EXPECT_THROW(Scene({ no_revision }), std::runtime_error);
  EXPECT_THROW(
    Scene({ with_surface }, { DistancePolicy() }), std::runtime_error);
}

//! The table is sorted and names each property and part once per node.
NOLINT_TEST(SceneOverridesTest, RejectsUnsortedOrRepeatedOverrides)
{
  const auto visible = SurfaceFlag(OverrideId::kGeometrySurfaceVisibility, 1U);
  const auto shadows
    = SurfaceFlag(OverrideId::kGeometrySurfaceCastsShadows, 1U);

  EXPECT_THROW(Scene({ shadows, visible }), std::runtime_error);
  EXPECT_THROW(Scene({ visible, visible }), std::runtime_error);
}

//! A bool is 0 or 1, an asset value is not nil, and a LOD policy names a
//! valid, otherwise unused entry of its record table.
NOLINT_TEST(SceneOverridesTest, RejectsMalformedValues)
{
  auto nil_material = world::OverrideRecord {
    .node_index = 0U,
    .override_id = OverrideId::kGeometrySurfaceMaterial,
    .part_id = Surface(),
    .layout_revision = Revision(),
  };
  auto decreasing = DistancePolicy();
  decreasing.boundary_count = 2U;
  decreasing.thresholds[1] = kLodDistance / 2.0F;

  EXPECT_THROW(
    Scene({ SurfaceFlag(OverrideId::kGeometrySurfaceVisibility, 2U) }),
    std::runtime_error);
  EXPECT_THROW(Scene({ nil_material }), std::runtime_error);
  EXPECT_THROW(
    Scene({ LodPolicyOverride(1U) }, { DistancePolicy() }), std::runtime_error);
  EXPECT_THROW(
    Scene({ LodPolicyOverride(0U) }, { decreasing }), std::runtime_error);
}

//! A LOD policy record no override names is refused.
NOLINT_TEST(SceneOverridesTest, RejectsUnusedLodPolicyRecords)
{
  EXPECT_THROW(
    Scene({ LodPolicyOverride(0U) }, { DistancePolicy(), DistancePolicy() }),
    std::runtime_error);
}

//! An override on a node without a renderable is refused.
NOLINT_TEST(SceneOverridesTest, RejectsOverridesOfNodesWithoutRenderable)
{
  auto record = SurfaceFlag(OverrideId::kGeometrySurfaceVisibility, 1U);
  record.node_index = 1U;
  auto builder = SceneAssetBuilder {};
  builder.AddNode("Mesh");
  builder.AddNode("Empty");
  builder.AddComponents(ComponentType::kRenderable,
    std::vector { world::RenderableRecord {
      .node_index = 0U,
      .geometry_key = data::AssetKey::FromVirtualPath("/test/Mesh.ogeo"),
    } });
  builder.AddComponents(ComponentType::kOverride, std::vector { record });

  EXPECT_THROW(static_cast<void>(builder.Build()), std::runtime_error);
}

} // namespace
