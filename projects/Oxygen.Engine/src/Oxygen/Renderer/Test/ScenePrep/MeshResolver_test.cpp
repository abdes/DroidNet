//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/ScenePrep/Extractors.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemProto.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Types/View.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
// glm types for transforms
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using oxygen::engine::View;
using oxygen::engine::sceneprep::ExtractionPreFilter;
using oxygen::engine::sceneprep::RenderItemProto;
using oxygen::engine::sceneprep::ScenePrepContext;
using oxygen::engine::sceneprep::ScenePrepState;

using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::MeshBuilder;
using oxygen::scene::DistancePolicy;
using oxygen::scene::FixedPolicy;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::ScreenSpaceErrorPolicy;

namespace {

class MeshResolverTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    scene_ = std::make_shared<Scene>("TestScene");
    node_ = scene_->CreateNode("TestNode");

    // Set the transform
    node_.GetTransform().SetLocalTransform(
      glm::vec3(0.2F), glm::quat(0.6F, 0.0F, 0.0F, 0.0F), glm::vec3(3.0f));
    scene_->Update();

    // Set rendering flags
    Flags().SetFlag(SceneNodeFlags::kCastsShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
    Flags().SetFlag(SceneNodeFlags::kReceivesShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));

    // Always add a geometry as the proto will abort if there is no geometry
    AddDefaultGeometry();
    proto_.emplace(node_.GetObject()->get());
    // Do not update the proto geometry or visibility. It is explicitly set in
    // the tests.

  ctx_.emplace(0, view_, *scene_);
  }

  void TearDown() override
  {
    // Clean up any shared resources here.
    scene_.reset();
  }

  void MarkDropped() { proto_->MarkDropped(); }
  void MarkVisible() { proto_->SetVisible(); }

  void SetView(const View& view)
  {
    view_ = view;
  ctx_.emplace(0, view_, *scene_);
  }

  void SetGeometry(const std::shared_ptr<GeometryAsset>& geometry)
  {
    Node().GetRenderable().SetGeometry(geometry);
    proto_->SetGeometry(geometry);
  }

  // Helper: set proto visible and seed world transform like ExtractionPreFilter
  void SeedVisibilityAndTransform()
  {
    proto_->SetVisible();
    proto_->SetWorldTransform(GetWorldMatrix());
  }

  // Helper: configure a simple View with explicit camera position and viewport
  // height; proj is identity except for m11 to get a positive focal length.
  void ConfigureView(glm::vec3 cam_pos, float viewport_height, float m11 = 1.0f)
  {
    View::Params p {};
    p.view = glm::mat4(1.0f);
    p.proj = glm::mat4(1.0f);
    p.proj[1][1] = m11;
    p.viewport = { 0.0f, 0.0f, 0.0f, viewport_height };
    p.has_camera_position = true;
    p.camera_position = cam_pos;
    SetView(View { p });
  }

  // Helper: build a simple triangle mesh (one submesh, one view)
  static auto MakeSimpleMesh(uint32_t lod, std::string_view name = {})
    -> std::shared_ptr<oxygen::data::Mesh>
  {
    using namespace oxygen::data;
    std::vector<Vertex> verts(3);
    // Give positions non-zero extents so per-LOD bounds are sane when used
    verts[0].position = { -1.0f, 0.0f, 0.0f };
    verts[1].position = { 1.0f, 0.0f, 0.0f };
    verts[2].position = { 0.0f, 1.0f, 0.0f };
    std::vector<uint32_t> idx = { 0, 1, 2 };
    auto mat = MaterialAsset::CreateDefault();
    auto builder = MeshBuilder(lod, name);
    builder.WithVertices(verts).WithIndices(idx);
    builder.BeginSubMesh("S0", mat)
      .WithMeshView({ .first_index = 0u,
        .index_count
        = static_cast<oxygen::data::pak::MeshViewDesc::BufferIndexT>(
          idx.size()),
        .first_vertex = 0u,
        .vertex_count
        = static_cast<oxygen::data::pak::MeshViewDesc::BufferIndexT>(
          verts.size()) })
      .EndSubMesh();
    return std::shared_ptr<Mesh>(builder.Build().release());
  }

  // Helper: build a GeometryAsset with N LODs and an asset-level AABB.
  static auto MakeGeometryWithLods(size_t lod_count, glm::vec3 bb_min,
    glm::vec3 bb_max) -> std::shared_ptr<GeometryAsset>
  {
    oxygen::data::pak::GeometryAssetDesc desc {};
    desc.lod_count = static_cast<uint32_t>(lod_count);
    desc.bounding_box_min[0] = bb_min.x;
    desc.bounding_box_min[1] = bb_min.y;
    desc.bounding_box_min[2] = bb_min.z;
    desc.bounding_box_max[0] = bb_max.x;
    desc.bounding_box_max[1] = bb_max.y;
    desc.bounding_box_max[2] = bb_max.z;

    std::vector<std::shared_ptr<oxygen::data::Mesh>> lods;
    lods.reserve(lod_count);
    for (size_t i = 0; i < lod_count; ++i) {
      lods.emplace_back(MakeSimpleMesh(static_cast<uint32_t>(i)));
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

  auto InvokeFilter()
  {
    return ExtractionPreFilter(Context(), State(), Proto());
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

    pak::GeometryAssetDesc desc {};
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

// Test: node invisible -> filter should return false
NOLINT_TEST_F(MeshResolverTest, DroppedItem_Death)
{
  MarkDropped();
  NOLINT_EXPECT_DEATH(MeshResolver(Context(), State(), Proto()), ".*");
}

// Test: node invisible -> filter should return false
NOLINT_TEST_F(MeshResolverTest, ProtoNoGeometry_Death)
{
  // Not Dropped
  // Geometry not explicitly set

  NOLINT_EXPECT_DEATH(MeshResolver(Context(), State(), Proto()), ".*");
}

} // namespace

//=== Positive paths: fixed LOD policy ===//----------------------------------//

NOLINT_TEST_F(MeshResolverTest, FixedPolicy_SelectsLOD0)
{
  // Arrange: 3 LOD geometry and fixed policy 0
  auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  Node().GetRenderable().SetLodPolicy(FixedPolicy { 0 });

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 0u);
  EXPECT_TRUE(static_cast<bool>(Proto().ResolvedMesh()));
}

NOLINT_TEST_F(MeshResolverTest, FixedPolicy_SelectsLOD2)
{
  // Arrange: 3 LOD geometry and fixed policy 2
  auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  Node().GetRenderable().SetLodPolicy(FixedPolicy { 2 });

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 2u);
  EXPECT_TRUE(static_cast<bool>(Proto().ResolvedMesh()));
}

//=== Distance policy: select based on normalized distance =================//

NOLINT_TEST_F(MeshResolverTest, DistancePolicy_Near_SelectsFineLOD)
{
  // Arrange
  auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  DistancePolicy dp { .thresholds = { 2.0f, 10.0f }, .hysteresis_ratio = 0.1f };
  Node().GetRenderable().SetLodPolicy(dp);

  // Place camera at the world-sphere center to get distance ~ 0
  const auto center = glm::vec3(GetWorldMatrix()[3]); // translation (0.2)
  ConfigureView(center, /*viewport_height*/ 720.0f, /*m11*/ 1.0f);

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: choose finest LOD 0
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 0u);
}

NOLINT_TEST_F(MeshResolverTest, DistancePolicy_Far_SelectsCoarseLOD)
{
  // Arrange
  auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  DistancePolicy dp { .thresholds = { 2.0f, 10.0f }, .hysteresis_ratio = 0.1f };
  Node().GetRenderable().SetLodPolicy(dp);

  // Far camera to make normalized distance >> thresholds
  const auto center = glm::vec3(GetWorldMatrix()[3]);
  ConfigureView(center + glm::vec3(100.0f, 0.0f, 0.0f), 720.0f, 1.0f);

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: choose coarsest LOD 2
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 2u);
}

//=== Screen-space error policy: selection via sse = f * r / z =============//

NOLINT_TEST_F(MeshResolverTest, ScreenSpaceErrorPolicy_NearHighSSE_SelectsFine)
{
  // Arrange
  auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  ScreenSpaceErrorPolicy sp { .enter_finer_sse = { 50.0f, 25.0f },
    .exit_coarser_sse = { 40.0f, 20.0f } };
  Node().GetRenderable().SetLodPolicy(sp);

  // Camera ~ at center -> z ~= 0 -> clamped to 1e-6 -> very large SSE
  const auto center = glm::vec3(GetWorldMatrix()[3]);
  ConfigureView(center, /*viewport_height*/ 1000.0f, /*m11*/ 1.0f);

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: select finest LOD 0
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 0u);
}

NOLINT_TEST_F(MeshResolverTest, ScreenSpaceErrorPolicy_FarLowSSE_SelectsCoarse)
{
  // Arrange
  auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  ScreenSpaceErrorPolicy sp { .enter_finer_sse = { 50.0f, 25.0f },
    .exit_coarser_sse = { 40.0f, 20.0f } };
  Node().GetRenderable().SetLodPolicy(sp);

  // Far camera -> small sse -> coarser LOD
  const auto center = glm::vec3(GetWorldMatrix()[3]);
  ConfigureView(center + glm::vec3(100.0f, 0.0f, 0.0f), /*height*/ 1000.0f,
    /*m11*/ 1.0f);

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: coarsest LOD 2
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 2u);
}

NOLINT_TEST_F(MeshResolverTest, ScreenSpaceErrorPolicy_NoFocal_FallbackLOD0)
{
  // Arrange: SSE policy but zero viewport height => focal length 0 => skip SSE
  auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  ScreenSpaceErrorPolicy sp { .enter_finer_sse = { 10.0f, 5.0f },
    .exit_coarser_sse = { 8.0f, 4.0f } };
  Node().GetRenderable().SetLodPolicy(sp);
  const auto center = glm::vec3(GetWorldMatrix()[3]);
  ConfigureView(center + glm::vec3(10.0f, 0.0f, 0.0f), /*height*/ 0.0f,
    /*m11*/ 1.0f);

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: no SSE selection performed -> default/fallback LOD 0
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 0u);
}

//=== Negative: Fixed policy index beyond LOD count clamps to last =========//

NOLINT_TEST_F(MeshResolverTest, FixedPolicy_IndexBeyondRange_ClampsToLast)
{
  // Arrange: geometry with 2 LODs but request LOD 10
  auto geom = MakeGeometryWithLods(2, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  Node().GetRenderable().SetLodPolicy(FixedPolicy { 10 });

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: clamped to last LOD (index 1)
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 1u);
  EXPECT_TRUE(static_cast<bool>(Proto().ResolvedMesh()));
}
