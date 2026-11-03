//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: SceneAsset override validation (ComponentType::kOverride and
// ComponentType::kOverrideLodPolicy).

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace world = data::pak::world;
namespace geometry = data::pak::geometry;
using data::ComponentType;
using data::CookedDefect;
using data::schema::OverrideId;
using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SceneNode;
using oxygen::cooker::testing::SceneRefusal;

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

auto SceneKey() -> data::AssetKey
{
  return data::AssetKey::FromVirtualPath("/test/Scene.oscene");
}

auto FlagValue(const uint8_t value) -> world::OverrideValue
{
  auto result = world::OverrideValue {};
  result.as_bool = value;
  return result;
}

auto MaterialValue() -> world::OverrideValue
{
  auto result = world::OverrideValue {};
  result.as_asset = data::AssetKey::FromVirtualPath("/test/Material.omat");
  return result;
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

//! Adds a node that renders a geometry to `scene`.
auto AddMesh(SceneBuilder& scene, const std::string_view name = "Mesh")
  -> SceneNode
{
  const auto mesh = scene.Node(name);
  scene.Renderable(mesh, data::AssetKey::FromVirtualPath("/test/Mesh.ogeo"));
  return mesh;
}

//! The record `index` of the override table of `descriptor`.
auto OverrideAt(const std::vector<std::byte>& descriptor, const uint32_t index)
  -> world::OverrideRecord
{
  return DescriptorDefects::ComponentAt<world::OverrideRecord>(
    descriptor, ComponentType::kOverride, index);
}

auto SetOverride(std::vector<std::byte>& descriptor, const uint32_t index,
  const world::OverrideRecord& record) -> void
{
  DescriptorDefects::SetComponent(
    descriptor, ComponentType::kOverride, index, record);
}

//! A descriptor of a mesh with a surface visibility override.
auto VisibilityScene() -> std::vector<std::byte>
{
  auto scene = SceneBuilder { "SceneOverridesTest" };
  const auto mesh = AddMesh(scene);
  scene.Override(mesh, OverrideId::kGeometrySurfaceVisibility, FlagValue(1U),
    Surface(), Revision());
  return scene.Bytes();
}

//! A descriptor of a mesh with a LOD policy.
auto LodPolicyScene() -> std::vector<std::byte>
{
  auto scene = SceneBuilder { "SceneOverridesTest" };
  scene.LodPolicy(AddMesh(scene), DistancePolicy());
  return scene.Bytes();
}

//! Each value type's override is accepted with a well-formed value.
NOLINT_TEST(SceneOverridesTest, AcceptsOneOverrideOfEachValueType)
{
  auto builder = SceneBuilder { "SceneOverridesTest" };
  const auto mesh = AddMesh(builder);
  builder.Override(mesh, OverrideId::kGeometrySurfaceMaterial, MaterialValue(),
    Surface(), Revision());
  builder.Override(mesh, OverrideId::kGeometrySurfaceVisibility, FlagValue(0U),
    Surface(), Revision());
  builder.Override(mesh, OverrideId::kGeometrySurfaceCastsShadows,
    FlagValue(1U), Surface(), Revision());
  builder.LodPolicy(mesh, DistancePolicy());

  const auto scene = builder.Build(SceneKey());

  EXPECT_EQ(scene->GetComponents<world::OverrideRecord>().size(), 4U);
  EXPECT_EQ(scene->GetComponents<geometry::LodPolicyRecord>().size(), 1U);
}

//! An id the schema does not declare is refused.
NOLINT_TEST(SceneOverridesTest, RejectsUnknownOverrideId)
{
  auto descriptor = VisibilityScene();
  auto record = OverrideAt(descriptor, 0U);
  record.override_id = OverrideId::kInvalid;
  SetOverride(descriptor, 0U, record);

  EXPECT_EQ(SceneRefusal(descriptor), CookedDefect::kInvalidValue);
}

//! A surface property needs its surface and layout revision; a component
//! property names neither.
NOLINT_TEST(SceneOverridesTest, RejectsPartsThatDoNotMatchTheProperty)
{
  auto no_surface = VisibilityScene();
  auto record = OverrideAt(no_surface, 0U);
  record.part_id = data::SurfaceId {};
  SetOverride(no_surface, 0U, record);

  auto no_revision = VisibilityScene();
  record = OverrideAt(no_revision, 0U);
  record.layout_revision = {};
  SetOverride(no_revision, 0U, record);

  auto with_surface = LodPolicyScene();
  record = OverrideAt(with_surface, 0U);
  record.part_id = Surface();
  SetOverride(with_surface, 0U, record);

  EXPECT_EQ(SceneRefusal(no_surface), CookedDefect::kMissingPart);
  EXPECT_EQ(SceneRefusal(no_revision), CookedDefect::kMissingPart);
  EXPECT_EQ(SceneRefusal(with_surface), CookedDefect::kInconsistent);
}

//! The table is sorted and names each property and part once per node.
NOLINT_TEST(SceneOverridesTest, RejectsUnsortedOrRepeatedOverrides)
{
  auto builder = SceneBuilder { "SceneOverridesTest" };
  const auto mesh = AddMesh(builder);
  builder.Override(mesh, OverrideId::kGeometrySurfaceVisibility, FlagValue(1U),
    Surface(), Revision());
  builder.Override(mesh, OverrideId::kGeometrySurfaceCastsShadows,
    FlagValue(1U), Surface(), Revision());
  const auto valid = builder.Bytes();
  const auto first = OverrideAt(valid, 0U);
  const auto second = OverrideAt(valid, 1U);

  auto unsorted = valid;
  SetOverride(unsorted, 0U, second);
  SetOverride(unsorted, 1U, first);
  auto repeated = valid;
  SetOverride(repeated, 1U, first);

  EXPECT_EQ(SceneRefusal(unsorted), CookedDefect::kNotCanonical);
  EXPECT_EQ(SceneRefusal(repeated), CookedDefect::kDuplicate);
}

//! A bool is 0 or 1, an asset value is not nil, and a LOD policy names a
//! valid, otherwise unused entry of its record table.
NOLINT_TEST(SceneOverridesTest, RejectsMalformedValues)
{
  auto bad_bool = VisibilityScene();
  auto record = OverrideAt(bad_bool, 0U);
  record.value.as_bool = 2U;
  SetOverride(bad_bool, 0U, record);

  auto material_scene = SceneBuilder { "SceneOverridesTest" };
  material_scene.Override(AddMesh(material_scene),
    OverrideId::kGeometrySurfaceMaterial, MaterialValue(), Surface(),
    Revision());
  auto nil_material = material_scene.Bytes();
  record = OverrideAt(nil_material, 0U);
  record.value.as_asset = data::AssetKey {};
  SetOverride(nil_material, 0U, record);

  auto past_the_table = LodPolicyScene();
  record = OverrideAt(past_the_table, 0U);
  record.value.as_record_index = world::OverrideRecordIndex { 1U };
  SetOverride(past_the_table, 0U, record);

  auto policy = DistancePolicy();
  policy.boundary_count = 2U;
  policy.thresholds[1] = kLodDistance * 2.0F;
  auto policy_scene = SceneBuilder { "SceneOverridesTest" };
  policy_scene.LodPolicy(AddMesh(policy_scene), policy);
  auto decreasing = policy_scene.Bytes();
  policy.thresholds[1] = kLodDistance / 2.0F;
  DescriptorDefects::SetComponent(
    decreasing, ComponentType::kOverrideLodPolicy, 0U, policy);

  EXPECT_EQ(SceneRefusal(bad_bool), CookedDefect::kInvalidValue);
  EXPECT_EQ(SceneRefusal(nil_material), CookedDefect::kMissingPart);
  EXPECT_EQ(SceneRefusal(past_the_table), CookedDefect::kInvalidReference);
  EXPECT_EQ(SceneRefusal(decreasing), CookedDefect::kInvalidValue);
}

//! A LOD policy record no override names is refused.
NOLINT_TEST(SceneOverridesTest, RejectsUnusedLodPolicyRecords)
{
  auto builder = SceneBuilder { "SceneOverridesTest" };
  builder.LodPolicy(AddMesh(builder, "First"), DistancePolicy());
  builder.LodPolicy(AddMesh(builder, "Second"), DistancePolicy());
  auto descriptor = builder.Bytes();
  auto record = OverrideAt(descriptor, 1U);
  record.value.as_record_index = world::OverrideRecordIndex { 0U };
  SetOverride(descriptor, 1U, record);

  EXPECT_EQ(SceneRefusal(descriptor), CookedDefect::kInconsistent);
}

//! An override on a node without a renderable is refused.
NOLINT_TEST(SceneOverridesTest, RejectsOverridesOfNodesWithoutRenderable)
{
  auto builder = SceneBuilder { "SceneOverridesTest" };
  const auto mesh = AddMesh(builder);
  const auto empty = builder.Node("Empty");
  builder.Override(mesh, OverrideId::kGeometrySurfaceVisibility, FlagValue(1U),
    Surface(), Revision());
  auto descriptor = builder.Bytes();
  auto record = OverrideAt(descriptor, 0U);
  record.target.node_index = empty.Index();
  SetOverride(descriptor, 0U, record);

  EXPECT_EQ(SceneRefusal(descriptor), CookedDefect::kInvalidReference);
}

} // namespace
