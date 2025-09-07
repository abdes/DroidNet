//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/Extraction/SceneExtraction.h>
#include <Oxygen/Renderer/RenderItemsList.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>

using oxygen::data::MaterialAsset;
using oxygen::data::MeshBuilder;
using oxygen::engine::RenderItemsList;

using oxygen::engine::extraction::CollectRenderItems;

namespace {

// Build a tiny mesh (triangle)
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

  auto material = MaterialAsset::CreateDefault();

  auto mesh = MeshBuilder()
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

// Wrap a single Mesh into a one-LOD GeometryAsset for scene attachment
static auto MakeSingleLodGeometry(std::shared_ptr<oxygen::data::Mesh> mesh)
  -> std::shared_ptr<const oxygen::data::GeometryAsset>
{
  using oxygen::data::GeometryAsset;
  using oxygen::data::pak::GeometryAssetDesc;

  GeometryAssetDesc desc {};
  desc.lod_count = 1;

  std::vector<std::shared_ptr<oxygen::data::Mesh>> lods;
  lods.push_back(std::move(mesh));
  return std::make_shared<GeometryAsset>(std::move(desc), std::move(lods));
}

static auto MakeDefaultView() -> View
{
  View::Params p;
  // Simple camera at origin looking -Z with identity projection for test
  p.view = glm::mat4(1.0F);
  p.proj = glm::perspective(glm::radians(60.0F), 1.0F, 0.1F, 100.0F);
  p.reverse_z = false;
  return View(p);
}

static auto MakeViewAtCameraZ(float cam_z) -> View
{
  View::Params p;
  // View matrix is inverse(camera world). To place camera at (0,0,cam_z),
  // set view = translate(I, -camera_pos).
  p.view = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 0.0F, -cam_z));
  p.proj = glm::perspective(glm::radians(60.0F), 1.0F, 0.1F, 10000.0F);
  p.reverse_z = false;
  return View(p);
}

NOLINT_TEST(SceneExtraction_BasicTest, TwoMeshes_OneInvisible_Culled)
{
  // Arrange: scene with two mesh nodes; hide one by moving far away
  auto scene = std::make_shared<oxygen::scene::Scene>("TestScene");
  auto a = scene->CreateNode("A");
  auto b = scene->CreateNode("B");

  const auto mesh = MakeUnitTriangleMesh();
  const auto geometry = MakeSingleLodGeometry(mesh);
  a.GetRenderable().SetGeometry(geometry);
  b.GetRenderable().SetGeometry(geometry);

  // Move A in front of the camera (into the frustum)
  auto at = a.GetTransform();
  at.SetLocalPosition(glm::vec3(0, 0, -5.0F));

  // Move B far beyond far plane (culled)
  auto bt = b.GetTransform();
  bt.SetLocalPosition(glm::vec3(0, 0, -500.0F));

  // Build a view and output list
  const View view = MakeDefaultView();
  RenderItemsList out;

  // Ensure transforms are up to date before extraction
  scene->Update(false);

  // Act
  const auto count = CollectRenderItems(*scene, view, out);

  // Assert: only A should be visible
  EXPECT_EQ(count, 1U);
  EXPECT_EQ(out.Size(), 1U);
}

NOLINT_TEST(SceneExtraction_EdgeTest, EmptyScene_YieldsZeroItems)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("Empty");
  const auto view = MakeDefaultView();
  RenderItemsList out;
  EXPECT_EQ(CollectRenderItems(*scene, view, out), 0U);
  EXPECT_EQ(out.Size(), 0U);
}

// Build a mesh with two far-apart submeshes and distinct materials
static auto MakeTwoSubmeshMesh(std::shared_ptr<const MaterialAsset> mat0,
  std::shared_ptr<const MaterialAsset> mat1)
  -> std::shared_ptr<oxygen::data::Mesh>
{
  using oxygen::data::Vertex;
  // Two separate right triangles in XY plane at z=0
  std::vector<Vertex> vertices = {
    // clang-format off
    // Submesh 0
    { .position = { 0.0F, 0.0F, 0.0F }, .normal = { 0, 0, 1 },
      .texcoord = { 0, 0 }, .tangent = { 1, 0, 0 }, .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 }, },
    { .position = { 1.0F, 0.0F, 0.0F }, .normal = { 0, 0, 1 },
      .texcoord = { 1, 0 }, .tangent = { 1, 0, 0 }, .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 }, },
    { .position = { 0.0F, 1.0F, 0.0F }, .normal = { 0, 0, 1 },
      .texcoord = { 0, 1 }, .tangent = { 1, 0, 0 }, .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 }, },

    // Submesh 1 shifted along +X by 10
    { .position = { 10.0F, 0.0F, 0.0F }, .normal = { 0, 0, 1 },
      .texcoord = { 0, 0 }, .tangent = { 1, 0, 0 }, .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 }, },
    { .position = { 11.0F, 0.0F, 0.0F }, .normal = { 0, 0, 1 },
      .texcoord = { 1, 0 }, .tangent = { 1, 0, 0 }, .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 }, },
    { .position = { 10.0F, 1.0F, 0.0F }, .normal = { 0, 0, 1 },
      .texcoord = { 0, 1 }, .tangent = { 1, 0, 0 }, .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 }, },
    // clang-format on
  };
  std::vector<std::uint32_t> indices = {
    0, 1, 2, // submesh 0
    3, 4, 5, // submesh 1
  };

  auto builder = MeshBuilder();
  builder.WithVertices(vertices).WithIndices(indices);

  builder.BeginSubMesh("A", std::move(mat0))
    .WithMeshView({ .first_index = 0,
      .index_count = 3,
      .first_vertex = 0,
      .vertex_count = 3 })
    .EndSubMesh()
    .BeginSubMesh("B", std::move(mat1))
    .WithMeshView({ .first_index = 3,
      .index_count = 3,
      .first_vertex = 3,
      .vertex_count = 3 })
    .EndSubMesh();

  return builder.Build();
}

// Build a GeometryAsset with two distinct LOD meshes
static auto MakeTwoLodGeometry(std::shared_ptr<oxygen::data::Mesh> lod0,
  std::shared_ptr<oxygen::data::Mesh> lod1)
  -> std::shared_ptr<const oxygen::data::GeometryAsset>
{
  using oxygen::data::GeometryAsset;
  using oxygen::data::pak::GeometryAssetDesc;

  GeometryAssetDesc desc {};
  desc.lod_count = 2;

  // Populate asset-level bounding box from the union of LOD meshes so that
  // dynamic LOD evaluation (before any selection) has a non-zero sphere.
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

// Scoped log capture using loguru callbacks for test assertions
class ScopedLogCapture {
public:
  ScopedLogCapture()
  {
    id_ = "SceneExtractionTest_LogCapture"; // unique enough in this TU
    loguru::add_callback(
      id_.c_str(), &ScopedLogCapture::OnLog, this, loguru::Verbosity_9);
  }
  ~ScopedLogCapture() { (void)loguru::remove_callback(id_.c_str()); }

  [[nodiscard]] bool Contains(std::string_view needle) const
  {
    for (const auto& msg : messages_) {
      if (msg.find(needle) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

private:
  static void OnLog(void* user_data, const loguru::Message& message)
  {
    auto* self = static_cast<ScopedLogCapture*>(user_data);
    if (!self) {
      return;
    }
    if (message.message) {
      self->messages_.emplace_back(message.message);
    }
  }

  std::string id_;
  std::vector<std::string> messages_;
};

//! All submeshes invisible → node skipped during extraction
NOLINT_TEST(SceneExtraction_Phase1, AllSubmeshesInvisible_SkipsNode)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("TestScene");
  auto node = scene->CreateNode("MeshNode");

  auto mat0 = MaterialAsset::CreateDefault();
  auto mat1 = MaterialAsset::CreateDefault();
  const auto mesh = MakeTwoSubmeshMesh(mat0, mat1);
  const auto geometry = MakeSingleLodGeometry(mesh);
  node.GetRenderable().SetGeometry(geometry);

  // Place node within the view frustum
  auto t = node.GetTransform();
  t.SetLocalPosition(glm::vec3(0, 0, -5.0F));

  // Hide all submeshes for LOD 0
  auto r = node.GetRenderable();
  r.SetAllSubmeshesVisible(false);

  const auto view = MakeDefaultView();
  RenderItemsList out;
  scene->Update(false);

  const auto count = CollectRenderItems(*scene, view, out);
  EXPECT_EQ(count, 0U);
  EXPECT_EQ(out.Size(), 0U);
}

//! Mixed materials → first visible submesh material selected and a debug log
//! is emitted
NOLINT_TEST(SceneExtraction_Phase1, MixedMaterials_PicksFirstVisible_Logs)
{
  auto scene = std::make_shared<oxygen::scene::Scene>("MixedMaterialsScene");
  auto node = scene->CreateNode("MeshNode");

  auto mat0 = MaterialAsset::CreateDefault();
  auto mat1 = MaterialAsset::CreateDefault();
  const auto mesh = MakeTwoSubmeshMesh(mat0, mat1);
  const auto geometry = MakeSingleLodGeometry(mesh);
  node.GetRenderable().SetGeometry(geometry);

  auto t = node.GetTransform();
  t.SetLocalPosition(glm::vec3(0, 0, -5.0F));

  // Both submeshes visible by default; install log capture
  ScopedLogCapture capture;

  const auto view = MakeDefaultView();
  RenderItemsList out;
  scene->Update(false);
  const auto count = CollectRenderItems(*scene, view, out);

  ASSERT_EQ(count, 1U);
  ASSERT_EQ(out.Size(), 1U);

  const auto items = out.Items();
  ASSERT_EQ(items.size(), 1U);
  // Must pick the material of the first visible submesh (index 0)
  EXPECT_EQ(items[0].material.get(), mat0.get());
  // Expect an informational/debug log about mixed materials
  EXPECT_TRUE(capture.Contains("mixed materials"));
}

//! Per-view LOD evaluation: the same node selects different LODs for
//! different views in the same frame (calls).
NOLINT_TEST(SceneExtraction_LOD, DistancePolicy_PerView_SelectsDifferentLods)
{
  using oxygen::scene::DistancePolicy;

  auto scene = std::make_shared<oxygen::scene::Scene>("PerViewLODScene");
  auto node = scene->CreateNode("LODNode");

  // Build two distinct LOD meshes
  auto lod0_mesh = MakeUnitTriangleMesh();
  auto lod1_mesh = MakeUnitTriangleMesh();
  const auto geometry = MakeTwoLodGeometry(lod0_mesh, lod1_mesh);
  node.GetRenderable().SetGeometry(geometry);

  // Force initial world bounds from LOD0 (sanity), then switch to Distance
  node.GetRenderable().SetLodPolicy(oxygen::scene::FixedPolicy { 0 });
  scene->Update(false);
  ASSERT_GT(node.GetRenderable().GetWorldBoundingSphere().w, 0.0F);

  // Switch to Distance policy with a clear boundary at 10x radius
  DistancePolicy dp;
  dp.thresholds = { 10.0F };
  dp.hysteresis_ratio = 0.0F; // eliminate sticky behavior for the test
  node.GetRenderable().SetLodPolicy(std::move(dp));

  // Recompute world sphere under DistancePolicy (uses asset-level sphere
  // before LOD evaluation). Use that radius for normalized distance math.
  scene->Update(false);
  const float r_eval = node.GetRenderable().GetWorldBoundingSphere().w;
  ASSERT_GT(r_eval, 0.0F);

  // Place the node on -Z so it's in front of a camera at origin
  node.GetTransform().SetLocalPosition(glm::vec3(0, 0, -2.0F * r_eval));
  scene->Update(false);

  // View A: camera at origin → distance ≈ 2r → normalized ≈ 2 < 10 ⇒ LOD0
  RenderItemsList out_a;
  const auto view_a = MakeViewAtCameraZ(0.0F);
  ASSERT_EQ(CollectRenderItems(*scene, view_a, out_a), 1U);
  ASSERT_EQ(out_a.Size(), 1U);
  EXPECT_EQ(out_a.Items()[0].mesh.get(), geometry->MeshAt(0).get());

  // View B: camera far along +Z → distance ≈ 102r → normalized ≈ 102 ⇒ LOD1
  RenderItemsList out_b;
  const auto view_b = MakeViewAtCameraZ(100.0F * r_eval);
  ASSERT_EQ(CollectRenderItems(*scene, view_b, out_b), 1U);
  ASSERT_EQ(out_b.Size(), 1U);
  EXPECT_EQ(out_b.Items()[0].mesh.get(), geometry->MeshAt(1).get());
}

} // namespace
