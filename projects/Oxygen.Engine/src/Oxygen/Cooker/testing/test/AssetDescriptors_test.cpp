//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

#include <glm/common.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/InputActionLoader.h>
#include <Oxygen/Content/Loaders/InputMappingContextLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/PhysicsAssetLoader.h>
#include <Oxygen/Cooker/testing/AssetDescriptors.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::cooker::testing::CollisionShapeDescriptor;
using oxygen::cooker::testing::GeometryBuilder;
using oxygen::cooker::testing::InputActionDescriptor;
using oxygen::cooker::testing::InputContextBuilder;
using oxygen::cooker::testing::MaterialBuilder;
using oxygen::cooker::testing::PhysicsMaterialDescriptor;
namespace data = oxygen::data;
namespace input = oxygen::data::pak::input;
namespace loaders = oxygen::content::loaders;
namespace physics = oxygen::data::pak::physics;
namespace render = oxygen::data::pak::render;

auto Key(const char* path) -> data::AssetKey
{
  return data::AssetKey::FromVirtualPath(path);
}

//! What the product loader `load` reads from `bytes`, checking that
//! `references` are the ones the descriptor uses.
template <typename Load>
auto Read(const std::span<const std::byte> bytes,
  const data::AssetReferences& references, Load load)
{
  auto stream = oxygen::serio::ReadOnlyMemoryStream { bytes };
  auto reader = oxygen::serio::Reader<oxygen::serio::ReadOnlyMemoryStream> {
    stream,
  };
  return load(oxygen::content::LoaderContext {
    .current_asset_key = {},
    .desc_reader = &reader,
    .asset_references = oxygen::observer_ptr { &references },
    .work_offline = true,
    .parse_only = true,
  });
}

//! A physics material reads back with its values and references nothing.
NOLINT_TEST(AssetDescriptorsTest, PhysicsMaterialReadsBackItsValues)
{
  const auto material
    = PhysicsMaterialDescriptor("Ice", { .static_friction = 0.05F });

  const auto read = Read(material.bytes, material.references,
    loaders::LoadPhysicsMaterialDescriptor);

  EXPECT_FLOAT_EQ(read.static_friction, 0.05F);
  EXPECT_TRUE(material.references.Keys().empty());
}

//! A collision shape references its material and its cooked payload, as the
//! collision shape cook records them.
NOLINT_TEST(AssetDescriptorsTest, CollisionShapeReferencesItsMaterialAndPayload)
{
  auto record = physics::CollisionShapeAssetDesc {};
  record.shape_type = physics::ShapeType::kConvexHull;
  record.material_asset_key = Key("/Game/Wood.opmat");
  record.cooked_shape_ref = {
    .payload_asset_key = Key("/Game/Crate.ophys"),
    .payload_type = physics::ShapePayloadType::kConvex,
  };

  const auto shape = CollisionShapeDescriptor("Crate", record);
  const auto read = Read(
    shape.bytes, shape.references, loaders::LoadCollisionShapeDescriptor);

  EXPECT_EQ(read.material_asset_key, Key("/Game/Wood.opmat"));
  EXPECT_EQ(shape.references.Keys().size(), 2U);
}

//! A shape the product refuses, such as one without a material, fails where
//! it is built.
NOLINT_TEST(AssetDescriptorsTest, CollisionShapeWithoutMaterialIsRefused)
{
  auto record = physics::CollisionShapeAssetDesc {};
  record.shape_type = physics::ShapeType::kSphere;

  EXPECT_THROW(static_cast<void>(CollisionShapeDescriptor("Ball", record)),
    std::logic_error);
}

//! An input action reads back with its flags and references nothing.
NOLINT_TEST(AssetDescriptorsTest, InputActionReadsBackItsFlags)
{
  const auto action = InputActionDescriptor(
    "Jump", { .flags = input::InputActionAssetFlags::kConsumesInput });

  const auto read
    = Read(action.bytes, action.references, loaders::LoadInputActionAsset);

  EXPECT_EQ(read->GetAssetName(), "Jump");
  EXPECT_EQ(read->GetFlags(), input::InputActionAssetFlags::kConsumesInput);
}

//! A context's mappings read back with their slots and triggers, and it
//! references every action its mappings and triggers name.
NOLINT_TEST(AssetDescriptorsTest, InputContextReadsBackMappingsAndTriggers)
{
  const auto jump = Key("/Game/Input/Jump.oiact");
  const auto crouch = Key("/Game/Input/Crouch.oiact");
  auto chord = input::InputTriggerRecord {};
  chord.type = input::InputTriggerType::kChord;
  chord.linked_action_asset_key = crouch;
  auto context = InputContextBuilder { "Gameplay" };
  context.Mapping(jump, "Space", { input::InputTriggerRecord {}, chord })
    .Mapping(crouch, "LeftCtrl");

  const auto references = context.References();
  const auto read
    = Read(context.Bytes(), references, loaders::LoadInputMappingContextAsset);

  ASSERT_EQ(read->GetMappings().size(), 2U);
  const auto& first = read->GetMappings().front();
  EXPECT_EQ(first.action_asset_key, jump);
  EXPECT_EQ(read->TryGetString(first.slot_name_offset), "Space");
  EXPECT_EQ(first.trigger_count, 2U);
  EXPECT_EQ(read->GetMappings().back().trigger_start_index, 2U);
  EXPECT_EQ(read->GetTriggers().size(), 2U);
  EXPECT_EQ(references.Keys().size(), 2U);
}

//! Two slots bound to one texture row share one binding; another source's
//! texture is bound by its key.
NOLINT_TEST(AssetDescriptorsTest, MaterialBindsItsTextures)
{
  constexpr auto kRow = oxygen::ResourceIndexT { 3U };
  const auto external = Key("/Props/Textures/Brick.otex");
  auto material = MaterialBuilder { "Wood" };
  material.Texture(&render::MaterialAssetDesc::base_color_texture, kRow)
    .Texture(&render::MaterialAssetDesc::normal_texture, kRow)
    .ExternalTexture(&render::MaterialAssetDesc::emissive_texture, external);

  const auto references = material.References();
  const auto read
    = Read(material.Bytes(), references, loaders::LoadMaterialAsset);

  EXPECT_EQ(read->GetAssetName(), "Wood");
  EXPECT_EQ(references.Resources().size(), 2U);
}

//! A geometry given no LOD is the procedural unit cube.
NOLINT_TEST(AssetDescriptorsTest, GeometryWithoutLodsIsTheUnitCube)
{
  const auto cube = GeometryBuilder { "Crate" };

  const auto references = cube.References();
  const auto read = Read(cube.Bytes(), references, loaders::LoadGeometryAsset);

  EXPECT_EQ(read->LodCount(), 1U);
  EXPECT_TRUE(references.Resources().empty());
  EXPECT_TRUE(references.Keys().empty());
  // Its bounds are those of the mesh the product generates.
  const auto generated = data::GenerateMeshBuffers("Cube/Mesh", {});
  ASSERT_HAS_VALUE(generated);
  auto low = generated->first.front().position;
  auto high = low;
  for (const auto& vertex : generated->first) {
    low = glm::min(low, vertex.position);
    high = glm::max(high, vertex.position);
  }
  EXPECT_EQ(read->BoundingBoxMin(), low);
  EXPECT_EQ(read->BoundingBoxMax(), high);
}

//! A standard LOD binds its buffer rows and every submesh material.
NOLINT_TEST(AssetDescriptorsTest, GeometryReferencesItsBuffersAndMaterials)
{
  auto geometry = GeometryBuilder { "Chair" };
  const auto lod = geometry.StandardLod(oxygen::ResourceIndexT { 1U },
    oxygen::ResourceIndexT { 2U }, glm::vec3 { -0.5F }, glm::vec3 { 0.5F });
  const auto triangle = data::pak::geometry::MeshViewDesc {
    .first_index = 0U,
    .index_count = 3U,
    .first_vertex = 0U,
    .vertex_count = 3U,
  };
  geometry
    .Submesh(lod, data::SurfaceId::FromStableIdentity("seat"),
      Key("/Props/Wood.omat"), { triangle })
    .Submesh(
      lod, data::SurfaceId::FromStableIdentity("legs"), {}, { triangle });

  const auto references = geometry.References();
  const auto read
    = Read(geometry.Bytes(), references, loaders::LoadGeometryAsset);

  EXPECT_EQ(read->LodCount(), 1U);
  EXPECT_EQ(references.Resources().size(), 2U);
  EXPECT_EQ(references.Keys().size(), 1U);
  // The standard LOD's authored bounds are its submeshes' and the geometry's.
  EXPECT_EQ(read->BoundingBoxMin(), glm::vec3 { -0.5F });
  EXPECT_EQ(read->BoundingBoxMax(), glm::vec3 { 0.5F });
  const auto& seat
    = read->MeshAt(oxygen::data::LodIndex { 0U })->SubMeshes().front();
  EXPECT_EQ(seat.BoundingBoxMin(), glm::vec3 { -0.5F });
  EXPECT_EQ(seat.BoundingBoxMax(), glm::vec3 { 0.5F });
}

} // namespace
