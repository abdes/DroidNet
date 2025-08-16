//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

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
// glm types for transforms
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using oxygen::engine::RenderContext;
using oxygen::engine::View;
using oxygen::engine::sceneprep::ExtractionPreFilter;
using oxygen::engine::sceneprep::RenderItemProto;
using oxygen::engine::sceneprep::ScenePrepContext;
using oxygen::engine::sceneprep::ScenePrepState;

using oxygen::data::MaterialAsset;
using oxygen::data::MeshBuilder;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

namespace {

class ExtractionFilterTest : public ::testing::Test {
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
    AddGeometry();
    proto_.emplace(node_.GetObject()->get());

    ctx_.emplace(0, *(static_cast<View*>(nullptr)), *scene_,
      *(static_cast<RenderContext*>(nullptr)));
  }

  void TearDown() override
  {
    // Clean up any shared resources here.
    scene_.reset();
  }

  auto& Context() { return *ctx_; }
  auto& State() { return state_; }
  auto& Proto() { return *proto_; }
  auto& Node() { return node_; }
  auto& Flags() { return node_.GetFlags()->get(); }

  auto GetWorldMatrix() const noexcept
  {
    return node_.GetTransform().GetWorldMatrix();
  }

  auto InvokeFilter()
  {
    return ExtractionPreFilter(Context(), State(), Proto());
  }

private:
  void AddGeometry()
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
  std::optional<ScenePrepContext> ctx_;
  ScenePrepState state_;
  std::optional<RenderItemProto> proto_;
};

// Test: node invisible -> filter should return false
NOLINT_TEST_F(ExtractionFilterTest, InvisibleNode_Fails)
{
  // Mark node as invisible via flags
  Flags().SetFlag(SceneNodeFlags::kVisible,
    oxygen::scene::SceneFlag {}.SetEffectiveValueBit(false));

  NOLINT_EXPECT_NO_THROW(InvokeFilter());
  EXPECT_TRUE(Proto().IsDropped());
}

// Test: node visible with geometry -> passes and proto seeded
NOLINT_TEST_F(ExtractionFilterTest, WithGeometry_PassesAndSeeds)
{
  // Geometry initialized in SetUp(); invoke filter and assert
  NOLINT_EXPECT_NO_THROW(ExtractionPreFilter(Context(), State(), Proto()));
  EXPECT_FALSE(Proto().IsDropped());
  EXPECT_TRUE(Proto().IsVisible());
  EXPECT_TRUE(Proto().CastsShadows());
  EXPECT_TRUE(Proto().ReceivesShadows());
  EXPECT_NE(Proto().Geometry(), nullptr);
  EXPECT_EQ(Proto().GetWorldTransform(), GetWorldMatrix());
}

} // namespace
