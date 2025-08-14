//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/Extraction/SceneExtraction.h>
#include <Oxygen/Renderer/RenderItemsList.h>
#include <Oxygen/Renderer/Types/View.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::data::MaterialAsset;
using oxygen::data::MeshBuilder;
using oxygen::engine::RenderItemsList;
using oxygen::engine::View;
using oxygen::engine::extraction::CollectRenderItems;

namespace {

// Build a tiny mesh (triangle)
static auto MakeUnitTriangleMesh() -> std::shared_ptr<const oxygen::data::Mesh>
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

static auto MakeDefaultView() -> View
{
  View::Params p;
  // Simple camera at origin looking -Z with identity projection for test
  p.view = glm::mat4(1.0F);
  p.proj = glm::perspective(glm::radians(60.0F), 1.0F, 0.1F, 100.0F);
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
  ASSERT_TRUE(a.AttachMesh(mesh));
  ASSERT_TRUE(b.AttachMesh(mesh));

  // Move A in front of the camera (into the frustum)
  auto at = a.GetTransform();
  at.SetLocalPosition(glm::vec3(0, 0, -5.0F));

  // Move B far beyond far plane (culled)
  auto bt = b.GetTransform();
  bt.SetLocalPosition(glm::vec3(0, 0, -500.0F));

  // Build a view and output list
  const View view = MakeDefaultView();
  RenderItemsList out;

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

} // namespace
