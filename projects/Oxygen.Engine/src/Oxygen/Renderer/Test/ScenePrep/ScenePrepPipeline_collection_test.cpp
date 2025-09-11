//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/glm.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/ScenePrep/CollectionConfig.h>
#include <Oxygen/Renderer/ScenePrep/FinalizationConfig.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

using namespace oxygen::engine;
using namespace oxygen::engine::sceneprep;
using oxygen::View;
using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::MeshBuilder;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;

namespace {

// Shared test fixture that builds a scene with two roots and a child under the
// first root. All nodes get minimal geometry (1 LOD, 1 submesh). Also provides
// a default View and per-test ScenePrepState.
class ScenePrepPipelineFixture : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    scene_ = std::make_shared<Scene>("TestScene");
    root_a_ = scene_->CreateNode("RootA");
    root_b_ = scene_->CreateNode("RootB");
    auto child_opt = scene_->CreateChildNode(root_a_, "ChildOfA");
    ASSERT_TRUE(child_opt.has_value());
    child_of_a_ = *child_opt;

    auto geom = BuildSimpleGeometry();
    root_a_.GetRenderable().SetGeometry(geom);
    root_b_.GetRenderable().SetGeometry(geom);
    child_of_a_.GetRenderable().SetGeometry(geom);
    scene_->Update();

    View::Params vp {};
    vp.view = glm::mat4(1.0f);
    vp.proj = glm::mat4(1.0f);
    vp.viewport = {
      .top_left_x = 0,
      .top_left_y = 0,
      .width = 0,
      .height = 600,
    };
    vp.has_camera_position = true;
    vp.camera_position = { 0.0f, 0.0f, 5.0f };
    view_ = View { vp };

    // rc_ default constructed
    state_ = std::make_unique<ScenePrepState>(nullptr, nullptr, nullptr);
  }

  static auto BuildSimpleGeometry() -> std::shared_ptr<GeometryAsset>
  {
    std::vector<oxygen::data::Vertex> vertices(3);
    std::vector<uint32_t> idx { 0, 1, 2 };
    auto mat = MaterialAsset::CreateDefault();
    auto mesh = MeshBuilder()
                  .WithVertices(vertices)
                  .WithIndices(idx)
                  .BeginSubMesh("S0", mat)
                  .WithMeshView({ .first_index = 0u,
                    .index_count = 3u,
                    .first_vertex = 0u,
                    .vertex_count = 3u })
                  .EndSubMesh()
                  .Build();
    oxygen::data::pak::GeometryAssetDesc desc {};
    desc.lod_count = 1;
    std::vector<std::shared_ptr<oxygen::data::Mesh>> lods;
    lods.emplace_back(std::shared_ptr<oxygen::data::Mesh>(mesh.release()));
    return std::make_shared<GeometryAsset>(desc, std::move(lods));
  }

  // Accessors for convenience
  [[nodiscard]] auto SceneRef() const -> const Scene& { return *scene_; }
  auto ViewRef() -> View& { return view_; }
  // ReSharper disable once CppMemberFunctionMayBeConst
  auto StateRef() -> ScenePrepState& { return *state_; }

  static constexpr size_t kNodeCount = 3; // RootA, RootB, ChildOfA

private:
  std::shared_ptr<Scene> scene_ {};
  SceneNode root_a_ {};
  SceneNode root_b_ {};
  SceneNode child_of_a_ {};
  View view_ { View::Params {} };
  std::unique_ptr<ScenePrepState> state_ {};
};

//! Verifies pipeline is testable by injecting custom stages and calling
//! Collect across a 3-node scene.
NOLINT_TEST_F(ScenePrepPipelineFixture,
  ScenePrepPipeline_Collection_CustomStages_ProducesPerNode)
{
  auto pre = [](const ScenePrepContext& /*ctx*/, ScenePrepState& /*st*/,
               RenderItemProto& it) {
    it.SetVisible();
    it.SetGeometry(it.Renderable().GetGeometry());
    it.SetWorldTransform(it.Transform().GetWorldMatrix());
  };
  auto resolve = [](const ScenePrepContext& /*ctx*/, ScenePrepState& /*st*/,
                   RenderItemProto& it) {
    if (auto g = it.Geometry()) {
      it.ResolveMesh(g->MeshAt(0), 0);
    } else {
      it.MarkDropped();
    }
  };
  auto vis = [](const ScenePrepContext& /*ctx*/, ScenePrepState& /*st*/,
               RenderItemProto& it) {
    if (!it.ResolvedMesh()) {
      it.MarkDropped();
      return;
    }
    it.SetVisibleSubmeshes({ 0u });
  };
  auto prod = [](const ScenePrepContext& /*ctx*/, ScenePrepState& st,
                RenderItemProto& it) {
    for (auto sm : it.VisibleSubmeshes()) {
      st.CollectItem(RenderItemData {
        .lod_index = it.ResolvedMeshIndex(),
        .submesh_index = sm,
        .geometry = it.Geometry(),
        .material = MaterialAsset::CreateDefault(),
        .world_bounding_sphere = it.Renderable().GetWorldBoundingSphere(),
        .cast_shadows = it.CastsShadows(),
        .receive_shadows = it.ReceivesShadows(),
      });
    }
  };

  using ConfigT = CollectionConfig<decltype(pre), void, decltype(resolve),
    decltype(vis), decltype(prod)>;
  ConfigT cfg {
    .pre_filter = pre,
    .mesh_resolver = resolve,
    .visibility_filter = vis,
    .producer = prod,
  };
  auto final_cfg = CreateStandardFinalizationConfig();
  std::unique_ptr<ScenePrepPipeline> pipeline
    = std::make_unique<ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
      cfg, final_cfg);

  pipeline->Collect(SceneRef(), ViewRef(), /*frame_id=*/1, StateRef(), true);

  ASSERT_EQ(StateRef().CollectedCount(), ScenePrepPipelineFixture::kNodeCount);
  for (const auto& item : StateRef().CollectedItems()) {
    EXPECT_EQ(item.lod_index, 0u);
    EXPECT_EQ(item.submesh_index, 0u);
    EXPECT_TRUE(item.geometry);
  }
}

//! Drop at pre-filter: downstream stages must not run; no items produced.
NOLINT_TEST_F(ScenePrepPipelineFixture,
  ScenePrepPipeline_Collection_DropAtPreFilter_SkipsDownstream)
{ // NOLINT
  int pre_called = 0, res_called = 0, vis_called = 0, prod_called = 0;
  auto pre
    = [&](const ScenePrepContext&, ScenePrepState&, RenderItemProto& it) {
        ++pre_called;
        it.MarkDropped();
      };
  auto resolve = [&](const ScenePrepContext&, ScenePrepState&,
                   RenderItemProto&) { ++res_called; };
  auto vis = [&](const ScenePrepContext&, ScenePrepState&, RenderItemProto&) {
    ++vis_called;
  };
  auto prod = [&](const ScenePrepContext&, ScenePrepState&, RenderItemProto&) {
    ++prod_called;
  };
  using ConfigT = CollectionConfig<decltype(pre), void, decltype(resolve),
    decltype(vis), decltype(prod)>;
  ConfigT cfg {
    .pre_filter = pre,
    .mesh_resolver = resolve,
    .visibility_filter = vis,
    .producer = prod,
  };
  auto final_cfg = CreateStandardFinalizationConfig();
  std::unique_ptr<ScenePrepPipeline> pipeline
    = std::make_unique<ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
      cfg, final_cfg);

  pipeline->Collect(SceneRef(), ViewRef(), /*frame_id=*/1, StateRef(), true);

  EXPECT_EQ(StateRef().CollectedCount(), 0);
  EXPECT_EQ(pre_called, static_cast<int>(ScenePrepPipelineFixture::kNodeCount));
  EXPECT_EQ(res_called, 0);
  EXPECT_EQ(vis_called, 0);
  EXPECT_EQ(prod_called, 0);
}

//! Drop at resolver: visibility and producer must not run; no items produced.
NOLINT_TEST_F(ScenePrepPipelineFixture,
  ScenePrepPipeline_Collection_DropAtResolver_SkipsDownstream)
{ // NOLINT
  int pre_called = 0, res_called = 0, vis_called = 0, prod_called = 0;
  auto pre
    = [&](const ScenePrepContext&, ScenePrepState&, RenderItemProto& it) {
        ++pre_called;
        it.SetVisible();
        it.SetGeometry(it.Renderable().GetGeometry());
        it.SetWorldTransform(it.Transform().GetWorldMatrix());
      };
  auto resolve
    = [&](const ScenePrepContext&, ScenePrepState&, RenderItemProto& it) {
        ++res_called;
        it.MarkDropped();
      };
  auto vis = [&](const ScenePrepContext&, ScenePrepState&, RenderItemProto&) {
    ++vis_called;
  };
  auto prod = [&](const ScenePrepContext&, ScenePrepState&, RenderItemProto&) {
    ++prod_called;
  };
  using ConfigT = CollectionConfig<decltype(pre), void, decltype(resolve),
    decltype(vis), decltype(prod)>;
  ConfigT cfg {
    .pre_filter = pre,
    .mesh_resolver = resolve,
    .visibility_filter = vis,
    .producer = prod,
  };
  auto final_cfg = CreateStandardFinalizationConfig();
  std::unique_ptr<ScenePrepPipeline> pipeline
    = std::make_unique<ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
      cfg, final_cfg);

  pipeline->Collect(SceneRef(), ViewRef(), /*frame_id=*/1, StateRef(), true);

  EXPECT_TRUE(StateRef().CollectedItems().empty());
  EXPECT_EQ(pre_called, static_cast<int>(ScenePrepPipelineFixture::kNodeCount));
  EXPECT_EQ(res_called, static_cast<int>(ScenePrepPipelineFixture::kNodeCount));
  EXPECT_EQ(vis_called, 0);
  EXPECT_EQ(prod_called, 0);
}

//! Drop at visibility filter: producer must not run; no items produced.
NOLINT_TEST_F(ScenePrepPipelineFixture,
  ScenePrepPipeline_Collection_DropAtVisibility_SkipsProducer)
{ // NOLINT
  int pre_called = 0, res_called = 0, vis_called = 0, prod_called = 0;
  auto pre
    = [&](const ScenePrepContext&, ScenePrepState&, RenderItemProto& it) {
        ++pre_called;
        it.SetVisible();
        it.SetGeometry(it.Renderable().GetGeometry());
        it.SetWorldTransform(it.Transform().GetWorldMatrix());
      };
  auto resolve
    = [&](const ScenePrepContext&, ScenePrepState&, RenderItemProto& it) {
        ++res_called;
        it.ResolveMesh(it.Geometry()->MeshAt(0), 0);
      };
  auto vis
    = [&](const ScenePrepContext&, ScenePrepState&, RenderItemProto& it) {
        ++vis_called;
        it.MarkDropped();
      };
  auto prod = [&](const ScenePrepContext&, ScenePrepState&, RenderItemProto&) {
    ++prod_called;
  };
  using ConfigT = CollectionConfig<decltype(pre), void, decltype(resolve),
    decltype(vis), decltype(prod)>;
  ConfigT cfg {
    .pre_filter = pre,
    .mesh_resolver = resolve,
    .visibility_filter = vis,
    .producer = prod,
  };
  auto final_cfg = CreateStandardFinalizationConfig();
  std::unique_ptr<ScenePrepPipeline> pipeline
    = std::make_unique<ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
      cfg, final_cfg);

  pipeline->Collect(SceneRef(), ViewRef(), /*frame_id=*/1, StateRef(), true);

  EXPECT_TRUE(StateRef().CollectedItems().empty());
  EXPECT_EQ(pre_called, static_cast<int>(ScenePrepPipelineFixture::kNodeCount));
  EXPECT_EQ(res_called, static_cast<int>(ScenePrepPipelineFixture::kNodeCount));
  EXPECT_EQ(vis_called, static_cast<int>(ScenePrepPipelineFixture::kNodeCount));
  EXPECT_EQ(prod_called, 0);
}

} // namespace
