//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <optional>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/ScenePrep/Extractors.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemProto.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Types/View.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

using oxygen::engine::RenderContext;
using oxygen::engine::View;
using oxygen::engine::sceneprep::EmitPerVisibleSubmesh;
using oxygen::engine::sceneprep::MeshResolver;
using oxygen::engine::sceneprep::RenderItemProto;
using oxygen::engine::sceneprep::ScenePrepContext;
using oxygen::engine::sceneprep::ScenePrepState;
using oxygen::engine::sceneprep::SubMeshVisibilityFilter;

using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::MeshBuilder;
using oxygen::scene::FixedPolicy;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

namespace {

class EmitPerVisibleSubmeshTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    scene_ = std::make_shared<Scene>("TestScene");
    node_ = scene_->CreateNode("TestNode");

    node_.GetTransform().SetLocalTransform(
      glm::vec3(0.0F), glm::quat(1.0F, 0.0F, 0.0F, 0.0F), glm::vec3(1.0f));
    scene_->Update();

    Flags().SetFlag(SceneNodeFlags::kCastsShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
    Flags().SetFlag(SceneNodeFlags::kReceivesShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));

    AddDefaultGeometry();
    proto_.emplace(node_.GetObject()->get());

    ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
    ctx_.emplace(0, view_, *scene_, *(static_cast<RenderContext*>(nullptr)));
  }

  void TearDown() override { scene_.reset(); }

  void ConfigurePerspectiveView(glm::vec3 eye, glm::vec3 center,
    glm::vec3 up = { 0, 1, 0 }, float fovy_deg = 60.0f, float aspect = 1.0f,
    float znear = 0.1f, float zfar = 1000.0f, int viewport = 1000)
  {
    View::Params p {};
    p.view = glm::lookAt(eye, center, up);
    p.proj = glm::perspective(glm::radians(fovy_deg), aspect, znear, zfar);
    p.viewport = { 0, 0, viewport, viewport };
    p.has_camera_position = true;
    p.camera_position = eye;
    view_ = View { p };
  }

  void SetGeometry(const std::shared_ptr<GeometryAsset>& geometry)
  {
    Node().GetRenderable().SetGeometry(geometry);
    proto_->SetGeometry(geometry);
  }

  void SeedVisibilityAndTransform()
  {
    proto_->SetVisible();
    proto_->SetWorldTransform(*node_.GetTransform().GetWorldMatrix());
  }

  // Build a mesh with N submeshes
  static auto MakeMeshWithSubmeshes(uint32_t lod, std::size_t submesh_count)
    -> std::shared_ptr<oxygen::data::Mesh>
  {
    using namespace oxygen::data;
    std::vector<Vertex> verts(4);
    verts[0].position = { -1, -1, 0 };
    verts[1].position = { 1, -1, 0 };
    verts[2].position = { 1, 1, 0 };
    verts[3].position = { -1, 1, 0 };
    std::vector<uint32_t> idx = { 0, 1, 2, 2, 3, 0 };
    auto mat = MaterialAsset::CreateDefault();
    MeshBuilder b(lod);
    b.WithVertices(verts).WithIndices(idx);
    for (std::size_t s = 0; s < submesh_count; ++s) {
      b.BeginSubMesh("SM", mat)
        .WithMeshView({ .first_index = 0u,
          .index_count
          = static_cast<oxygen::data::pak::MeshViewDesc::BufferIndexT>(
            idx.size()),
          .first_vertex = 0u,
          .vertex_count
          = static_cast<oxygen::data::pak::MeshViewDesc::BufferIndexT>(
            verts.size()) })
        .EndSubMesh();
    }
    return std::shared_ptr<Mesh>(b.Build().release());
  }

  // Build geometry with a single LOD and specified submesh count
  static auto MakeGeometryWithSubmeshes(std::size_t submesh_count)
    -> std::shared_ptr<GeometryAsset>
  {
    oxygen::data::pak::GeometryAssetDesc desc {};
    desc.lod_count = 1;
    desc.bounding_box_min[0] = -1.0f;
    desc.bounding_box_min[1] = -1.0f;
    desc.bounding_box_min[2] = -1.0f;
    desc.bounding_box_max[0] = 1.0f;
    desc.bounding_box_max[1] = 1.0f;
    desc.bounding_box_max[2] = 1.0f;
    auto mesh = MakeMeshWithSubmeshes(0, submesh_count);
    return std::make_shared<GeometryAsset>(
      desc, std::vector<std::shared_ptr<oxygen::data::Mesh>> { mesh });
  }

  auto& Context() { return *ctx_; }
  auto& State() { return state_; }
  auto& Proto() { return *proto_; }
  auto& Node() { return node_; }
  auto& Flags() { return node_.GetFlags()->get(); }

private:
  void AddDefaultGeometry()
  {
    using namespace oxygen::data;
    std::vector<Vertex> verts(3);
    std::vector<uint32_t> idx = { 0, 1, 2 };
    auto mat = MaterialAsset::CreateDefault();
    std::shared_ptr<Mesh> mesh = MeshBuilder()
                                   .WithVertices(verts)
                                   .WithIndices(idx)
                                   .BeginSubMesh("s", mat)
                                   .WithMeshView({ .first_index = 0,
                                     .index_count = 3,
                                     .first_vertex = 0,
                                     .vertex_count = 3 })
                                   .EndSubMesh()
                                   .Build();

    oxygen::data::pak::GeometryAssetDesc desc {};
    desc.lod_count = 1;
    std::vector<std::shared_ptr<Mesh>> lods;
    lods.push_back(mesh);
    auto geometry
      = std::make_shared<GeometryAsset>(std::move(desc), std::move(lods));

    Node().GetRenderable().SetGeometry(geometry);
  }

  std::shared_ptr<Scene> scene_;
  SceneNode node_;
  View view_ { View::Params {} };
  std::optional<ScenePrepContext> ctx_;
  ScenePrepState state_;
  std::optional<RenderItemProto> proto_;
};

// Death: dropped item
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, DroppedItem_Death)
{
  Proto().MarkDropped();
  NOLINT_EXPECT_DEATH(EmitPerVisibleSubmesh(Context(), State(), Proto()), ".*");
}

// Death: no resolved mesh
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, NoResolvedMesh_Death)
{
  // Ensure geometry/visibility seeded but skip MeshResolver
  SeedVisibilityAndTransform();
  NOLINT_EXPECT_DEATH(EmitPerVisibleSubmesh(Context(), State(), Proto()), ".*");
}

// Empty visible list -> emits nothing
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, EmptyVisibleList_NoEmission)
{
  // Arrange: resolve mesh but clear visible list
  auto geom = MakeGeometryWithSubmeshes(2);
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  MeshResolver(Context(), State(), Proto());
  // Do not run visibility filter; set empty list directly
  Proto().SetVisibleSubmeshes({});

  // Act
  EmitPerVisibleSubmesh(Context(), State(), Proto());

  // Assert
  EXPECT_TRUE(State().collected_items.empty());
}

// Emit one item per visible submesh with correct properties
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, EmitsAllVisible_WithExpectedFields)
{
  // Arrange: geometry with 3 submeshes, all visible
  auto geom = MakeGeometryWithSubmeshes(3);
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  MeshResolver(Context(), State(), Proto());
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Act
  EmitPerVisibleSubmesh(Context(), State(), Proto());

  // Assert
  const auto& items = State().collected_items;
  ASSERT_EQ(items.size(), 3u);
  for (size_t i = 0; i < items.size(); ++i) {
    EXPECT_EQ(items[i].lod_index, Proto().ResolvedMeshIndex());
    EXPECT_EQ(items[i].submesh_index, i);
    EXPECT_EQ(items[i].geometry.get(), Proto().Geometry().get());
    EXPECT_EQ(items[i].world_transform, Proto().GetWorldTransform());
    EXPECT_EQ(items[i].world_bounding_sphere,
      Node().GetRenderable().GetWorldBoundingSphere());
    EXPECT_EQ(items[i].cast_shadows, Proto().CastsShadows());
    EXPECT_EQ(items[i].receive_shadows, Proto().ReceivesShadows());
  }
}

// Material override takes precedence over mesh submesh material
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, MaterialOverride_TakesPrecedence)
{
  // Arrange: 2 submeshes; override submesh 1
  auto geom = MakeGeometryWithSubmeshes(2);
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  MeshResolver(Context(), State(), Proto());
  SubMeshVisibilityFilter(Context(), State(), Proto());

  const auto lod = Proto().ResolvedMeshIndex();
  auto override_mat = MaterialAsset::CreateDefault();
  Node().GetRenderable().SetMaterialOverride(lod, 1, override_mat);

  // Act
  EmitPerVisibleSubmesh(Context(), State(), Proto());

  // Assert: find submesh 1 item and check material ptr
  const auto& items = State().collected_items;
  auto it = std::find_if(items.begin(), items.end(),
    [](const auto& r) { return r.submesh_index == 1u; });
  ASSERT_NE(it, items.end());
  EXPECT_EQ(it->material, override_mat);
}

// No override -> mesh submesh material is used
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, MeshMaterial_UsedWhenNoOverride)
{
  // Arrange: 2 submeshes; no overrides
  auto geom = MakeGeometryWithSubmeshes(2);
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  MeshResolver(Context(), State(), Proto());
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // The mesh's submesh material is the one attached in the builder
  const auto mesh_material = Proto().ResolvedMesh()->SubMeshes()[0].Material();

  // Act
  EmitPerVisibleSubmesh(Context(), State(), Proto());

  // Assert: find submesh 0 item and check material ptr equals mesh material
  const auto& items = State().collected_items;
  auto it = std::find_if(items.begin(), items.end(),
    [](const auto& r) { return r.submesh_index == 0u; });
  ASSERT_NE(it, items.end());
  EXPECT_EQ(it->material, mesh_material);
}

// Masked out submesh should not be emitted
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, MaskedOutSubmesh_NotEmitted)
{
  auto geom = MakeGeometryWithSubmeshes(3);
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  MeshResolver(Context(), State(), Proto());

  // Hide submesh 1, keep others visible
  const auto lod = Proto().ResolvedMeshIndex();
  Node().GetRenderable().SetSubmeshVisible(lod, 1, false);

  SubMeshVisibilityFilter(Context(), State(), Proto());
  EmitPerVisibleSubmesh(Context(), State(), Proto());

  // Expect only 0 and 2 emitted
  const auto& items = State().collected_items;
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0].submesh_index, 0u);
  EXPECT_EQ(items[1].submesh_index, 2u);
}

} // namespace
