//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Renderer/Extraction/RenderListBuilder.h>
#include <Oxygen/Renderer/Types/View.h>
#include <Oxygen/Scene/Scene.h>

using oxygen::engine::RenderContext;
using oxygen::engine::RenderItemsList;
using oxygen::engine::extraction::RenderItemData;
using oxygen::engine::extraction::RenderListBuilder;

namespace {

// Build a tiny mesh (triangle) - copied from SceneExtraction_test helper
static auto MakeUnitTriangleMesh() -> std::shared_ptr<oxygen::data::Mesh>
{
  std::vector<oxygen::data::Vertex> vertices = {
    {
      .position = { 0.0F, 0.0F, 0.0F },
      .normal = { 0, 0, 1 },
      .texcoord = { 0, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 },
    },
    {
      .position = { 1.0F, 0.0F, 0.0F },
      .normal = { 0, 0, 1 },
      .texcoord = { 1, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 },
    },
    {
      .position = { 0.0F, 1.0F, 0.0F },
      .normal = { 0, 0, 1 },
      .texcoord = { 0, 1 },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 },
    },
  };
  std::vector<std::uint32_t> indices = { 0, 1, 2 };

  auto material = oxygen::data::MaterialAsset::CreateDefault();

  auto mesh = oxygen::data::MeshBuilder()
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("DefaultSubMesh", material)
                .WithMeshView({ .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = 3 })
                .EndSubMesh()
                .Build();
  return mesh;
}

// Wrap two Mesh into a two-LOD GeometryAsset (copied pattern)
static auto MakeTwoLodGeometry(std::shared_ptr<oxygen::data::Mesh> lod0,
  std::shared_ptr<oxygen::data::Mesh> lod1)
  -> std::shared_ptr<const oxygen::data::GeometryAsset>
{
  using oxygen::data::GeometryAsset;
  using oxygen::data::pak::GeometryAssetDesc;

  GeometryAssetDesc desc {};
  desc.lod_count = 2;

  // Populate asset-level bounding box from the union of LOD meshes
  const auto min0 = lod0 ? lod0->BoundingBoxMin() : glm::vec3(0.0f);
  const auto max0 = lod0 ? lod0->BoundingBoxMax() : glm::vec3(0.0f);
  const auto min1 = lod1 ? lod1->BoundingBoxMin() : glm::vec3(0.0f);
  const auto max1 = lod1 ? lod1->BoundingBoxMax() : glm::vec3(0.0f);
  const glm::vec3 bb_min = glm::min(min0, min1);
  const glm::vec3 bb_max = glm::max(max0, max1);
  desc.bounding_box_min[0] = bb_min.x;
  desc.bounding_box_min[1] = bb_min.y;
  desc.bounding_box_min[2] = bb_min.z;
  desc.bounding_box_max[0] = bb_max.x;
  desc.bounding_box_max[1] = bb_max.y;
  desc.bounding_box_max[2] = bb_max.z;

  std::vector<std::shared_ptr<oxygen::data::Mesh>> lods;
  lods.push_back(std::move(lod0));
  lods.push_back(std::move(lod1));
  return std::make_shared<GeometryAsset>(std::move(desc), std::move(lods));
}

//! Basic smoke tests for RenderListBuilder
TEST(RenderListBuilder_Basic, Smoke)
{
  // Arrange
  RenderListBuilder builder;

  // Create a minimal shared Scene (required because Collect calls
  // shared_from_this()) and a trivial View so the builder can run safely.
  auto scene = std::make_shared<oxygen::scene::Scene>("test_scene");
  oxygen::engine::View::Params vp {};
  oxygen::engine::View view(vp);

  // Act
  auto collected = builder.Collect(*scene, view, 0);

  // Assert - empty scene -> no items
  EXPECT_TRUE(collected.empty());
}

//! LOD selection via RenderListBuilder: Distance policy should select
//! different LODs per view (reuses SceneExtraction test pattern).
TEST(RenderListBuilder_LOD, DistancePolicy_PerView_SelectsDifferentLods)
{
  using oxygen::scene::DistancePolicy;

  // Arrange
  RenderListBuilder builder;
  auto scene = std::make_shared<oxygen::scene::Scene>("PerViewLODScene");
  auto node = scene->CreateNode("LODNode");

  // Build two distinct LOD meshes using local helpers
  auto lod0_mesh = MakeUnitTriangleMesh();
  auto lod1_mesh = MakeUnitTriangleMesh();
  const auto geometry = MakeTwoLodGeometry(lod0_mesh, lod1_mesh);
  node.GetRenderable().SetGeometry(geometry);

  // Force an initial policy and update scene so world bounds are valid
  node.GetRenderable().SetLodPolicy(oxygen::scene::FixedPolicy { 0 });
  scene->Update(false);

  // Switch to Distance policy with a clear threshold at 10x radius
  DistancePolicy dp;
  dp.thresholds = { 10.0F };
  dp.hysteresis_ratio = 0.0F;
  node.GetRenderable().SetLodPolicy(std::move(dp));

  // Recompute world sphere under DistancePolicy and place node at 2*r
  scene->Update(false);
  const float r_eval = node.GetRenderable().GetWorldBoundingSphere().w;
  node.GetTransform().SetLocalPosition(glm::vec3(0, 0, -2.0F * r_eval));
  scene->Update(false);

  // View A: camera at origin -> expect LOD0
  oxygen::engine::View::Params vpA {};
  oxygen::engine::View view_a(vpA);
  auto collected_a = builder.Collect(*scene, view_a, 0);
  ASSERT_EQ(collected_a.size(), 1U);
  EXPECT_EQ(collected_a[0].geometry->MeshAt(collected_a[0].lod_index).get(),
    geometry->MeshAt(0).get());

  // View B: camera far along +Z -> expect LOD1
  oxygen::engine::View::Params vpB {};
  // Place camera far along +Z by translating view matrix
  vpB.view
    = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 0.0F, -100.0F * r_eval));
  vpB.proj = glm::perspective(glm::radians(60.0F), 1.0F, 0.1F, 10000.0F);
  oxygen::engine::View view_b(vpB);
  auto collected_b = builder.Collect(*scene, view_b, 0);
  ASSERT_EQ(collected_b.size(), 1U);
  EXPECT_EQ(collected_b[0].geometry->MeshAt(collected_b[0].lod_index).get(),
    geometry->MeshAt(1).get());
}

} // namespace
