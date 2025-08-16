//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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

using oxygen::engine::RenderContext;
using oxygen::engine::View;
using oxygen::engine::sceneprep::MeshResolver;
using oxygen::engine::sceneprep::RenderItemProto;
using oxygen::engine::sceneprep::ScenePrepContext;
using oxygen::engine::sceneprep::ScenePrepState;
using oxygen::engine::sceneprep::SubMeshVisibilityFilter;

using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::scene::FixedPolicy;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

namespace {

class SubMeshVisibilityFilterTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    scene_ = std::make_shared<Scene>("TestScene");
    node_ = scene_->CreateNode("TestNode");

    // Transform (non-identity to ensure world sphere non-degenerate)
    node_.GetTransform().SetLocalTransform(
      glm::vec3(0.2F), glm::quat(0.6F, 0.0F, 0.0F, 0.0F), glm::vec3(3.0f));
    scene_->Update();

    // Rendering flags
    Flags().SetFlag(SceneNodeFlags::kCastsShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
    Flags().SetFlag(SceneNodeFlags::kReceivesShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));

    // Ensure node has a default geometry before constructing the proto
    AddDefaultGeometry();
    // Proto seeded with node; proto geometry/visibility set per test explicitly
    proto_.emplace(node_.GetObject()->get());

    ctx_.emplace(0, view_, *scene_, *(static_cast<RenderContext*>(nullptr)));
  }

  void TearDown() override { scene_.reset(); }

  void MarkDropped() { proto_->MarkDropped(); }

  void SetView(const View& view)
  {
    view_ = view;
    ctx_.emplace(0, view_, *scene_, *(static_cast<RenderContext*>(nullptr)));
  }

  void SeedVisibilityAndTransform()
  {
    proto_->SetVisible();
    proto_->SetWorldTransform(GetWorldMatrix());
  }

  // Minimal view with explicit camera position (affects MeshResolver only)
  void ConfigureView(glm::vec3 cam_pos, int viewport_height, float m11 = 1.0f)
  {
    View::Params p {};
    p.view = glm::mat4(1.0f);
    p.proj = glm::mat4(1.0f);
    p.proj[1][1] = m11;
    p.viewport = { 0, 0, 0, viewport_height };
    p.has_camera_position = true;
    p.camera_position = cam_pos;
    SetView(View { p });
  }

  // Proper perspective view for frustum-based tests
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
    SetView(View { p });
  }

  void SetGeometry(const std::shared_ptr<GeometryAsset>& geometry)
  {
    Node().GetRenderable().SetGeometry(geometry);
    proto_->SetGeometry(geometry);
  }

  // Build mesh with arbitrary number of submeshes; each submesh has one view
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

  // Build a mesh with N submeshes, each occupying a distinct region in space
  // via disjoint vertex/index ranges. Centers specify the quad centers.
  static auto MakeSpreadMesh(
    uint32_t lod, const std::vector<glm::vec3>& centers)
    -> std::shared_ptr<oxygen::data::Mesh>
  {
    using namespace oxygen::data;
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    verts.reserve(centers.size() * 4);
    idx.reserve(centers.size() * 6);
    auto mat = MaterialAsset::CreateDefault();
    MeshBuilder b(lod);

    for (size_t s = 0; s < centers.size(); ++s) {
      const auto base_v = static_cast<uint32_t>(verts.size());
      const glm::vec3 c = centers[s];
      Vertex v0 {}, v1 {}, v2 {}, v3 {};
      v0.position = c + glm::vec3(-1, -1, 0);
      v1.position = c + glm::vec3(1, -1, 0);
      v2.position = c + glm::vec3(1, 1, 0);
      v3.position = c + glm::vec3(-1, 1, 0);
      verts.push_back(v0);
      verts.push_back(v1);
      verts.push_back(v2);
      verts.push_back(v3);
      const auto base_i = static_cast<uint32_t>(idx.size());
      idx.push_back(base_v + 0);
      idx.push_back(base_v + 1);
      idx.push_back(base_v + 2);
      idx.push_back(base_v + 2);
      idx.push_back(base_v + 3);
      idx.push_back(base_v + 0);

      if (s == 0) {
        b.WithVertices(verts).WithIndices(idx);
      } else {
        // Update builder's buffers in-place by re-calling setters
        b.WithVertices(verts).WithIndices(idx);
      }
      b.BeginSubMesh("SMs", mat)
        .WithMeshView({ .first_index = base_i,
          .index_count
          = static_cast<oxygen::data::pak::MeshViewDesc::BufferIndexT>(6),
          .first_vertex = base_v,
          .vertex_count
          = static_cast<oxygen::data::pak::MeshViewDesc::BufferIndexT>(4) })
        .EndSubMesh();
    }

    return std::shared_ptr<Mesh>(b.Build().release());
  }

  // Build geometry with per-LOD submesh counts
  static auto MakeGeometryWithLODSubmeshes(
    std::initializer_list<std::size_t> per_lod_counts)
    -> std::shared_ptr<GeometryAsset>
  {
    oxygen::data::pak::GeometryAssetDesc desc {};
    desc.lod_count = static_cast<uint32_t>(per_lod_counts.size());
    desc.bounding_box_min[0] = -1.0f;
    desc.bounding_box_min[1] = -1.0f;
    desc.bounding_box_min[2] = -1.0f;
    desc.bounding_box_max[0] = 1.0f;
    desc.bounding_box_max[1] = 1.0f;
    desc.bounding_box_max[2] = 1.0f;

    std::vector<std::shared_ptr<oxygen::data::Mesh>> lods;
    lods.reserve(per_lod_counts.size());
    uint32_t lod = 0;
    for (auto count : per_lod_counts) {
      lods.emplace_back(MakeMeshWithSubmeshes(lod++, count));
    }
    return std::make_shared<GeometryAsset>(std::move(desc), std::move(lods));
  }

  auto& Context() { return *ctx_; }
  auto& State() { return state_; }
  auto& Proto() { return *proto_; }
  auto& Node() { return node_; }
  auto& Flags() { return node_.GetFlags()->get(); }

  auto GetWorldMatrix() const noexcept
  {
    return *node_.GetTransform().GetWorldMatrix();
  }

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
NOLINT_TEST_F(SubMeshVisibilityFilterTest, DroppedItem_Death)
{
  // Arrange
  MarkDropped();
  // Act + Assert
  NOLINT_EXPECT_DEATH(
    SubMeshVisibilityFilter(Context(), State(), Proto()), ".*");
}

// Death: proto has no geometry
NOLINT_TEST_F(SubMeshVisibilityFilterTest, ProtoNoGeometry_Death)
{
  // Geometry not explicitly set
  NOLINT_EXPECT_DEATH(
    SubMeshVisibilityFilter(Context(), State(), Proto()), ".*");
}

// If no mesh is resolved, the item is marked dropped and visible list remains
// empty
NOLINT_TEST_F(SubMeshVisibilityFilterTest, NoResolvedMesh_MarksDropped)
{
  // Arrange
  auto geom = MakeGeometryWithLODSubmeshes({ 3 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();

  // Do NOT run MeshResolver here -> ResolvedMesh() is null

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert
  EXPECT_TRUE(Proto().IsDropped());
  EXPECT_TRUE(Proto().VisibleSubmeshes().empty());
}

// All submeshes visible -> indices [0..N-1]
NOLINT_TEST_F(SubMeshVisibilityFilterTest, AllVisible_CollectsAllIndices)
{
  // Arrange: 1 LOD with 3 submeshes, resolve mesh
  auto geom = MakeGeometryWithLODSubmeshes({ 3 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();

  // Resolve via MeshResolver (fixed policy default to LOD0)
  // Use a proper perspective view to keep the mesh in frustum
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
  MeshResolver(Context(), State(), Proto());

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert
  const auto vis = Proto().VisibleSubmeshes();
  ASSERT_EQ(vis.size(), 3u);
  EXPECT_EQ(vis[0], 0u);
  EXPECT_EQ(vis[1], 1u);
  EXPECT_EQ(vis[2], 2u);
}

// Some hidden -> only visible indices are collected
NOLINT_TEST_F(SubMeshVisibilityFilterTest, SomeHidden_FiltersOutHidden)
{
  // Arrange: 1 LOD with 4 submeshes
  auto geom = MakeGeometryWithLODSubmeshes({ 4 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
  MeshResolver(Context(), State(), Proto());

  const auto lod = Proto().ResolvedMeshIndex();
  // Hide 1 and 3
  Node().GetRenderable().SetSubmeshVisible(lod, 1, false);
  Node().GetRenderable().SetSubmeshVisible(lod, 3, false);

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert
  const auto vis = Proto().VisibleSubmeshes();
  ASSERT_EQ(vis.size(), 2u);
  EXPECT_EQ(vis[0], 0u);
  EXPECT_EQ(vis[1], 2u);
}

// Different LODs: ensure selection uses active LOD submesh set
NOLINT_TEST_F(SubMeshVisibilityFilterTest, MultiLOD_UsesActiveLODSubmeshes)
{
  // Arrange: LOD0 has 2 submeshes, LOD1 has 1
  auto geom = MakeGeometryWithLODSubmeshes({ 2, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  // Force LOD1 (coarser) via fixed policy
  Node().GetRenderable().SetLodPolicy(FixedPolicy { 1 });
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
  MeshResolver(Context(), State(), Proto());

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert: only submesh 0 exists at LOD1
  const auto vis = Proto().VisibleSubmeshes();
  ASSERT_EQ(vis.size(), 1u);
  EXPECT_EQ(vis[0], 0u);
}

// All hidden -> visible list becomes empty
NOLINT_TEST_F(SubMeshVisibilityFilterTest, AllHidden_ResultsInEmptyList)
{
  auto geom = MakeGeometryWithLODSubmeshes({ 3 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
  MeshResolver(Context(), State(), Proto());

  Node().GetRenderable().SetAllSubmeshesVisible(false);

  SubMeshVisibilityFilter(Context(), State(), Proto());

  EXPECT_TRUE(Proto().VisibleSubmeshes().empty());
}

// Frustum: looking away from the object -> all submeshes culled
NOLINT_TEST_F(SubMeshVisibilityFilterTest, Frustum_AllOutside_RemovesAll)
{
  // Arrange: 1 LOD with 3 submeshes
  auto geom = MakeGeometryWithLODSubmeshes({ 3 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  // Camera looks away from origin so geometry at z≈0 is behind frustum
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 10));
  MeshResolver(Context(), State(), Proto());

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert: all culled
  EXPECT_TRUE(Proto().VisibleSubmeshes().empty());
}

// Frustum: spread submeshes across X; only the center is visible
NOLINT_TEST_F(SubMeshVisibilityFilterTest, Frustum_PartialVisible_SelectsSubset)
{
  // Arrange: single LOD with 3 submeshes at X=-100,0,100
  using oxygen::data::Mesh;
  std::vector<glm::vec3> centers
    = { { -100.f, 0.f, 0.f }, { 0.f, 0.f, 0.f }, { 100.f, 0.f, 0.f } };
  auto mesh = MakeSpreadMesh(0, centers);
  oxygen::data::pak::GeometryAssetDesc desc {};
  desc.lod_count = 1;
  auto geom = std::make_shared<oxygen::data::GeometryAsset>(
    desc, std::vector<std::shared_ptr<Mesh>> { mesh });

  SetGeometry(geom);
  SeedVisibilityAndTransform();
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
  MeshResolver(Context(), State(), Proto());

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert: only the middle submesh (index 1) is inside the frustum
  const auto vis = Proto().VisibleSubmeshes();
  ASSERT_EQ(vis.size(), 1u);
  EXPECT_EQ(vis[0], 1u);
}

} // namespace
