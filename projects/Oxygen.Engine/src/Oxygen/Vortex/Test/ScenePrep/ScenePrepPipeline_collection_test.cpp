//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedLogCapture.h>
#include <Oxygen/Vortex/ScenePrep/CollectionConfig.h>
#include <Oxygen/Vortex/ScenePrep/FinalizationConfig.h>
#include <Oxygen/Vortex/ScenePrep/MaterialRef.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemData.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemProto.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepContext.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepPipeline.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepState.h>
#include <Oxygen/Vortex/Test/ScenePrep/ScenePrepHelpers.h>

namespace sceneprep = oxygen::vortex::sceneprep;
namespace sceneprep_testing = oxygen::vortex::sceneprep::testing;

using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;

namespace {

// Shared test fixture that builds a scene with two roots and a child under the
// first root. All nodes get minimal geometry (1 LOD, 1 submesh). Also provides
// a default View and per-test ScenePrepState.
class ScenePrepPipelineTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    constexpr std::size_t kTestSceneCapacity = 100;
    scene_ = std::make_shared<Scene>("TestScene", kTestSceneCapacity);
    root_a_ = scene_->CreateNode("RootA");
    root_b_ = scene_->CreateNode("RootB");
    const auto child_opt = scene_->CreateChildNode(root_a_, "ChildOfA");
    if (!child_opt.has_value()) {
      FAIL() << "Expected child_opt to have a value";
    }
    child_of_a_ = *child_opt;

    const auto geom = BuildSimpleGeometry();
    root_a_.GetRenderable().SetGeometry(geom);
    root_b_.GetRenderable().SetGeometry(geom);
    child_of_a_.GetRenderable().SetGeometry(geom);
    scene_->Update();

    oxygen::ResolvedView::Params vp {};
    vp.view_matrix = glm::mat4(1.0F);
    vp.proj_matrix = glm::mat4(1.0F);
    vp.view_config.viewport = {
      .top_left_x = 0,
      .top_left_y = 0,
      .width = 0,
      .height = 600,
    };
    vp.camera_position = {
      0.0F,
      0.0F,
      5.0F,
    };
    vp.near_plane = 0.1F;
    vp.far_plane = 1000.0F;
    view_ = std::make_shared<oxygen::ResolvedView>(vp);

    state_
      = std::make_unique<sceneprep::ScenePrepState>(nullptr, nullptr, nullptr);
  }

  static auto BuildSimpleGeometry() -> std::shared_ptr<GeometryAsset>
  {
    return sceneprep_testing::MakeGeometryWithLods(
      1, glm::vec3(-1.0F), glm::vec3(1.0F));
  }

  // Accessors for convenience
  [[nodiscard]] auto SceneRef() const -> const Scene& { return *scene_; }
  [[nodiscard]] auto ResolvedViewRef() const -> const oxygen::ResolvedView&
  {
    return *view_;
  }
  [[nodiscard]] auto ViewObserver() const
    -> oxygen::observer_ptr<const oxygen::ResolvedView>
  {
    return oxygen::observer_ptr<const oxygen::ResolvedView>(view_.get());
  }
  // ReSharper disable once CppMemberFunctionMayBeConst
  auto StateRef() -> sceneprep::ScenePrepState& { return *state_; }

  static constexpr size_t kNodeCount = 3; // RootA, RootB, ChildOfA

private:
  std::shared_ptr<Scene> scene_;
  SceneNode root_a_ {};
  SceneNode root_b_ {};
  SceneNode child_of_a_ {};
  std::shared_ptr<oxygen::ResolvedView> view_
    = std::make_shared<oxygen::ResolvedView>(oxygen::ResolvedView::Params {
      .near_plane = 0.1F,
      .far_plane = 1000.0F,
    });
  std::unique_ptr<sceneprep::ScenePrepState> state_;
};

auto MakeContractTestPipeline() -> std::unique_ptr<sceneprep::ScenePrepPipeline>
{
  auto pre = [](const sceneprep::ScenePrepContext& /*ctx*/,
               sceneprep::ScenePrepState& /*st*/,
               sceneprep::RenderItemProto& it) -> void {
    it.SetVisible();
    it.SetGeometry(it.Renderable().GetGeometry());
    it.SetWorldTransform(it.Transform().GetWorldMatrix());
  };
  auto resolve = [](const sceneprep::ScenePrepContext& /*ctx*/,
                   sceneprep::ScenePrepState& /*st*/,
                   sceneprep::RenderItemProto& it) -> void {
    if (const auto g = it.Geometry()) {
      it.ResolveMesh(g->MeshAt(0), 0);
    } else {
      it.MarkDropped();
    }
  };
  auto vis = [](const sceneprep::ScenePrepContext& /*ctx*/,
               sceneprep::ScenePrepState& /*st*/,
               sceneprep::RenderItemProto& it) -> void {
    if (!it.ResolvedMesh()) {
      it.MarkDropped();
      return;
    }
    it.SetVisibleSubmeshes({
      0U,
    });
  };
  auto prod
    = [](const sceneprep::ScenePrepContext& /*ctx*/,
        sceneprep::ScenePrepState& st, sceneprep::RenderItemProto& it) -> void {
    for (const auto sm : it.VisibleSubmeshes()) {
      const auto default_material = MaterialAsset::CreateDefault();
      const auto default_material_key = default_material != nullptr
        ? default_material->GetAssetKey()
        : oxygen::data::AssetKey {};

      auto item = sceneprep::RenderItemData {};
      item.submesh_index = static_cast<std::uint32_t>(sm);
      item.geometry = sceneprep::GeometryRef {
        .asset_key = it.Geometry()->GetAssetKey(),
        .lod_index = static_cast<std::uint32_t>(it.ResolvedMeshIndex()),
        .mesh = it.ResolvedMesh(),
      };
      item.material = sceneprep::MaterialRef {
        .source_asset_key = default_material_key,
        .resolved_asset_key = default_material_key,
        .resolved_asset = default_material,
      };
      item.world_bounding_sphere = it.Renderable().GetWorldBoundingSphere();
      item.cast_shadows = it.CastsShadows();
      item.receive_shadows = it.ReceivesShadows();
      st.CollectItem(std::move(item));
    }
  };

  using ConfigT = sceneprep::CollectionConfig<decltype(pre), void,
    decltype(resolve), decltype(vis), decltype(prod)>;
  ConfigT cfg {
    .pre_filter = pre,
    .mesh_resolver = resolve,
    .visibility_filter = vis,
    .producer = prod,
  };
  auto final_cfg = sceneprep::CreateStandardFinalizationConfig();
  return std::make_unique<
    sceneprep::ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
    cfg, final_cfg);
}

//! Verifies the phase-explicit runtime contract still produces one item per
//! visible test node.
NOLINT_TEST_F(
  ScenePrepPipelineTest, PhaseExplicitCollection_CustomStages_ProducesPerNode)
{
  auto pre = [](const sceneprep::ScenePrepContext& /*ctx*/,
               sceneprep::ScenePrepState& /*st*/,
               sceneprep::RenderItemProto& it) -> void {
    it.SetVisible();
    it.SetGeometry(it.Renderable().GetGeometry());
    it.SetWorldTransform(it.Transform().GetWorldMatrix());
  };
  auto resolve = [](const sceneprep::ScenePrepContext& /*ctx*/,
                   sceneprep::ScenePrepState& /*st*/,
                   sceneprep::RenderItemProto& it) -> void {
    if (const auto g = it.Geometry()) {
      it.ResolveMesh(g->MeshAt(0), 0);
    } else {
      it.MarkDropped();
    }
  };
  auto vis = [](const sceneprep::ScenePrepContext& /*ctx*/,
               sceneprep::ScenePrepState& /*st*/,
               sceneprep::RenderItemProto& it) -> void {
    if (!it.ResolvedMesh()) {
      it.MarkDropped();
      return;
    }
    it.SetVisibleSubmeshes({
      0U,
    });
  };
  auto prod
    = [](const sceneprep::ScenePrepContext& /*ctx*/,
        sceneprep::ScenePrepState& st, sceneprep::RenderItemProto& it) -> void {
    for (const auto sm : it.VisibleSubmeshes()) {
      const auto default_material = MaterialAsset::CreateDefault();
      const auto default_material_key = default_material
        ? default_material->GetAssetKey()
        : oxygen::data::AssetKey {};

      auto item = sceneprep::RenderItemData {};
      item.submesh_index = static_cast<std::uint32_t>(sm);
      item.geometry = sceneprep::GeometryRef {
        .asset_key = it.Geometry()->GetAssetKey(),
        .lod_index = static_cast<std::uint32_t>(it.ResolvedMeshIndex()),
        .mesh = it.ResolvedMesh(),
      };
      item.material = sceneprep::MaterialRef {
        .source_asset_key = default_material_key,
        .resolved_asset_key = default_material_key,
        .resolved_asset = default_material,
      };
      item.world_bounding_sphere = it.Renderable().GetWorldBoundingSphere();
      item.cast_shadows = it.CastsShadows();
      item.receive_shadows = it.ReceivesShadows();
      st.CollectItem(std::move(item));
    }
  };

  using ConfigT = sceneprep::CollectionConfig<decltype(pre), void,
    decltype(resolve), decltype(vis), decltype(prod)>;
  ConfigT cfg {
    .pre_filter = pre,
    .mesh_resolver = resolve,
    .visibility_filter = vis,
    .producer = prod,
  };
  auto final_cfg = sceneprep::CreateStandardFinalizationConfig();
  const std::unique_ptr<sceneprep::ScenePrepPipeline> pipeline
    = std::make_unique<
      sceneprep::ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
      cfg, final_cfg);

  // Run frame-phase first (no view) to populate cached filtered node list,
  // then run per-view phase with a real ResolvedView so per-view stages
  // execute.
  pipeline->BeginFrameCollection(SceneRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  pipeline->PrepareView(SceneRef(), ResolvedViewRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  pipeline->FinalizeView(StateRef());

  ASSERT_EQ(StateRef().CollectedCount(), ScenePrepPipelineTest::kNodeCount);
  for (const auto& item : StateRef().CollectedItems()) {
    EXPECT_EQ(item.geometry.lod_index, 0U);
    EXPECT_EQ(item.submesh_index, 0U);
    EXPECT_TRUE(item.geometry.IsValid());
  }
}

NOLINT_TEST_F(
  ScenePrepPipelineTest, PrepareView_ResetDoesNotAppendPreviousViewItems)
{
  auto pre = [](const sceneprep::ScenePrepContext& /*ctx*/,
               sceneprep::ScenePrepState& /*st*/,
               sceneprep::RenderItemProto& it) -> void {
    it.SetVisible();
    it.SetGeometry(it.Renderable().GetGeometry());
    it.SetWorldTransform(it.Transform().GetWorldMatrix());
  };
  auto resolve = [](const sceneprep::ScenePrepContext& /*ctx*/,
                   sceneprep::ScenePrepState& /*st*/,
                   sceneprep::RenderItemProto& it) -> void {
    if (const auto g = it.Geometry()) {
      it.ResolveMesh(g->MeshAt(0), 0);
    } else {
      it.MarkDropped();
    }
  };
  auto vis = [](const sceneprep::ScenePrepContext& /*ctx*/,
               sceneprep::ScenePrepState& /*st*/,
               sceneprep::RenderItemProto& it) -> void {
    if (!it.ResolvedMesh()) {
      it.MarkDropped();
      return;
    }
    it.SetVisibleSubmeshes({
      0U,
    });
  };
  auto prod
    = [](const sceneprep::ScenePrepContext& /*ctx*/,
        sceneprep::ScenePrepState& st, sceneprep::RenderItemProto& it) -> void {
    const auto default_material = MaterialAsset::CreateDefault();
    const auto default_material_key = default_material != nullptr
      ? default_material->GetAssetKey()
      : oxygen::data::AssetKey {};

    auto item = sceneprep::RenderItemData {};
    item.submesh_index = 0U;
    item.geometry = sceneprep::GeometryRef {
      .asset_key = it.Geometry()->GetAssetKey(),
      .lod_index = static_cast<std::uint32_t>(it.ResolvedMeshIndex()),
      .mesh = it.ResolvedMesh(),
    };
    item.material = sceneprep::MaterialRef {
      .source_asset_key = default_material_key,
      .resolved_asset_key = default_material_key,
      .resolved_asset = default_material,
    };
    item.world_bounding_sphere = it.Renderable().GetWorldBoundingSphere();
    item.cast_shadows = it.CastsShadows();
    item.receive_shadows = it.ReceivesShadows();
    st.CollectItem(std::move(item));
  };

  using ConfigT = sceneprep::CollectionConfig<decltype(pre), void,
    decltype(resolve), decltype(vis), decltype(prod)>;
  ConfigT cfg {
    .pre_filter = pre,
    .mesh_resolver = resolve,
    .visibility_filter = vis,
    .producer = prod,
  };
  auto final_cfg = sceneprep::CreateStandardFinalizationConfig();
  const std::unique_ptr<sceneprep::ScenePrepPipeline> pipeline
    = std::make_unique<
      sceneprep::ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
      cfg, final_cfg);

  pipeline->BeginFrameCollection(SceneRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  pipeline->PrepareView(SceneRef(), ResolvedViewRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  ASSERT_EQ(StateRef().CollectedCount(), ScenePrepPipelineTest::kNodeCount);
  pipeline->FinalizeView(StateRef());

  pipeline->PrepareView(SceneRef(), ResolvedViewRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  EXPECT_EQ(StateRef().CollectedCount(), ScenePrepPipelineTest::kNodeCount);
  pipeline->FinalizeView(StateRef());
}

NOLINT_TEST_F(ScenePrepPipelineTest,
  PhaseExplicitCollection_ViewResetPreservesFrameCachesAndFrameResetRebuildsThem)
{
  int pre_called = 0;
  auto pre = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
               sceneprep::RenderItemProto& it) -> void {
    ++pre_called;
    it.SetVisible();
    it.SetGeometry(it.Renderable().GetGeometry());
    it.SetWorldTransform(it.Transform().GetWorldMatrix());
  };
  auto resolve
    = [](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
        sceneprep::RenderItemProto& it) -> void {
    if (const auto g = it.Geometry()) {
      it.ResolveMesh(g->MeshAt(0), 0);
    } else {
      it.MarkDropped();
    }
  };
  auto vis = [](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
               sceneprep::RenderItemProto& it) -> void {
    if (!it.ResolvedMesh()) {
      it.MarkDropped();
      return;
    }
    it.SetVisibleSubmeshes({
      0U,
    });
  };
  auto prod
    = [](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState& st,
        sceneprep::RenderItemProto& it) -> void {
    const auto default_material = MaterialAsset::CreateDefault();
    const auto default_material_key = default_material != nullptr
      ? default_material->GetAssetKey()
      : oxygen::data::AssetKey {};

    auto item = sceneprep::RenderItemData {};
    item.submesh_index = 0U;
    item.geometry = sceneprep::GeometryRef {
      .asset_key = it.Geometry()->GetAssetKey(),
      .lod_index = static_cast<std::uint32_t>(it.ResolvedMeshIndex()),
      .mesh = it.ResolvedMesh(),
    };
    item.material = sceneprep::MaterialRef {
      .source_asset_key = default_material_key,
      .resolved_asset_key = default_material_key,
      .resolved_asset = default_material,
    };
    item.world_bounding_sphere = it.Renderable().GetWorldBoundingSphere();
    item.cast_shadows = it.CastsShadows();
    item.receive_shadows = it.ReceivesShadows();
    st.CollectItem(std::move(item));
  };

  using ConfigT = sceneprep::CollectionConfig<decltype(pre), void,
    decltype(resolve), decltype(vis), decltype(prod)>;
  ConfigT cfg {
    .pre_filter = pre,
    .mesh_resolver = resolve,
    .visibility_filter = vis,
    .producer = prod,
  };
  auto final_cfg = sceneprep::CreateStandardFinalizationConfig();
  const std::unique_ptr<sceneprep::ScenePrepPipeline> pipeline
    = std::make_unique<
      sceneprep::ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
      cfg, final_cfg);

  pipeline->BeginFrameCollection(SceneRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());

  ASSERT_EQ(pre_called, static_cast<int>(ScenePrepPipelineTest::kNodeCount));
  ASSERT_EQ(StateRef().GetFilteredSceneNodes().size(),
    ScenePrepPipelineTest::kNodeCount);
  ASSERT_FALSE(StateRef().GetFilteredSceneNodes().empty());
  EXPECT_NE(
    StateRef().TryGetNodeBasics(StateRef().GetFilteredSceneNodes().front()),
    nullptr);

  pipeline->PrepareView(SceneRef(), ResolvedViewRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  EXPECT_EQ(pre_called, static_cast<int>(ScenePrepPipelineTest::kNodeCount));
  EXPECT_EQ(StateRef().GetFilteredSceneNodes().size(),
    ScenePrepPipelineTest::kNodeCount);
  pipeline->FinalizeView(StateRef());

  pipeline->PrepareView(SceneRef(), ResolvedViewRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  EXPECT_EQ(pre_called, static_cast<int>(ScenePrepPipelineTest::kNodeCount));
  EXPECT_EQ(StateRef().GetFilteredSceneNodes().size(),
    ScenePrepPipelineTest::kNodeCount);
  pipeline->FinalizeView(StateRef());

  pipeline->BeginFrameCollection(SceneRef(),
    oxygen::frame::SequenceNumber {
      2,
    },
    StateRef());

  EXPECT_EQ(
    pre_called, 2 * static_cast<int>(ScenePrepPipelineTest::kNodeCount));
  ASSERT_EQ(StateRef().GetFilteredSceneNodes().size(),
    ScenePrepPipelineTest::kNodeCount);
  EXPECT_NE(
    StateRef().TryGetNodeBasics(StateRef().GetFilteredSceneNodes().front()),
    nullptr);
}

//! Drop at pre-filter: downstream stages must not run; no items produced.
NOLINT_TEST_F(ScenePrepPipelineTest,
  PhaseExplicitCollection_DropAtPreFilter_SkipsDownstream)
{
  int pre_called = 0;
  int res_called = 0;
  int vis_called = 0;
  int prod_called = 0;
  auto pre = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
               sceneprep::RenderItemProto& it) -> void {
    ++pre_called;
    it.MarkDropped();
  };
  auto resolve
    = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
        sceneprep::RenderItemProto&) -> void { ++res_called; };
  auto vis = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
               sceneprep::RenderItemProto&) -> void { ++vis_called; };
  auto prod
    = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
        sceneprep::RenderItemProto&) -> void { ++prod_called; };
  using ConfigT = sceneprep::CollectionConfig<decltype(pre), void,
    decltype(resolve), decltype(vis), decltype(prod)>;
  ConfigT cfg {
    .pre_filter = pre,
    .mesh_resolver = resolve,
    .visibility_filter = vis,
    .producer = prod,
  };
  auto final_cfg = sceneprep::CreateStandardFinalizationConfig();
  const std::unique_ptr<sceneprep::ScenePrepPipeline> pipeline
    = std::make_unique<
      sceneprep::ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
      cfg, final_cfg);

  pipeline->BeginFrameCollection(SceneRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  pipeline->PrepareView(SceneRef(), ResolvedViewRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());

  EXPECT_EQ(StateRef().CollectedCount(), 0);
  // pre_filter runs in frame-phase and view-phase reuses cached basics.
  EXPECT_EQ(pre_called, static_cast<int>(ScenePrepPipelineTest::kNodeCount));
  EXPECT_EQ(res_called, 0);
  EXPECT_EQ(vis_called, 0);
  EXPECT_EQ(prod_called, 0);
}

//! Drop at resolver: visibility and producer must not run; no items produced.
NOLINT_TEST_F(
  ScenePrepPipelineTest, PhaseExplicitCollection_DropAtResolver_SkipsDownstream)
{
  int pre_called = 0;
  int res_called = 0;
  int vis_called = 0;
  int prod_called = 0;
  auto pre = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
               sceneprep::RenderItemProto& it) -> void {
    ++pre_called;
    it.SetVisible();
    it.SetGeometry(it.Renderable().GetGeometry());
    it.SetWorldTransform(it.Transform().GetWorldMatrix());
  };
  auto resolve
    = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
        sceneprep::RenderItemProto& it) -> void {
    ++res_called;
    it.MarkDropped();
  };
  auto vis = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
               sceneprep::RenderItemProto&) -> void { ++vis_called; };
  auto prod
    = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
        sceneprep::RenderItemProto&) -> void { ++prod_called; };
  using ConfigT = sceneprep::CollectionConfig<decltype(pre), void,
    decltype(resolve), decltype(vis), decltype(prod)>;
  ConfigT cfg {
    .pre_filter = pre,
    .mesh_resolver = resolve,
    .visibility_filter = vis,
    .producer = prod,
  };
  auto final_cfg = sceneprep::CreateStandardFinalizationConfig();
  const std::unique_ptr<sceneprep::ScenePrepPipeline> pipeline
    = std::make_unique<
      sceneprep::ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
      cfg, final_cfg);

  pipeline->BeginFrameCollection(SceneRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  pipeline->PrepareView(SceneRef(), ResolvedViewRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());

  EXPECT_TRUE(StateRef().CollectedItems().empty());
  // pre_filter runs in frame-phase and view-phase reuses cached basics.
  EXPECT_EQ(pre_called, static_cast<int>(ScenePrepPipelineTest::kNodeCount));
  EXPECT_EQ(res_called, static_cast<int>(ScenePrepPipelineTest::kNodeCount));
  EXPECT_EQ(vis_called, 0);
  EXPECT_EQ(prod_called, 0);
}

//! Drop at visibility filter: producer must not run; no items produced.
NOLINT_TEST_F(
  ScenePrepPipelineTest, PhaseExplicitCollection_DropAtVisibility_SkipsProducer)
{
  int pre_called = 0;
  int res_called = 0;
  int vis_called = 0;
  int prod_called = 0;
  auto pre = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
               sceneprep::RenderItemProto& it) -> void {
    ++pre_called;
    it.SetVisible();
    it.SetGeometry(it.Renderable().GetGeometry());
    it.SetWorldTransform(it.Transform().GetWorldMatrix());
  };
  auto resolve
    = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
        sceneprep::RenderItemProto& it) -> void {
    ++res_called;
    it.ResolveMesh(it.Geometry()->MeshAt(0), 0);
  };
  auto vis = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
               sceneprep::RenderItemProto& it) -> void {
    ++vis_called;
    it.MarkDropped();
  };
  auto prod
    = [&](const sceneprep::ScenePrepContext&, sceneprep::ScenePrepState&,
        sceneprep::RenderItemProto&) -> void { ++prod_called; };
  using ConfigT = sceneprep::CollectionConfig<decltype(pre), void,
    decltype(resolve), decltype(vis), decltype(prod)>;
  ConfigT cfg {
    .pre_filter = pre,
    .mesh_resolver = resolve,
    .visibility_filter = vis,
    .producer = prod,
  };
  auto final_cfg = sceneprep::CreateStandardFinalizationConfig();
  const std::unique_ptr<sceneprep::ScenePrepPipeline> pipeline
    = std::make_unique<
      sceneprep::ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
      cfg, final_cfg);

  pipeline->BeginFrameCollection(SceneRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  pipeline->PrepareView(SceneRef(), ResolvedViewRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());

  EXPECT_TRUE(StateRef().CollectedItems().empty());
  // pre_filter runs in frame-phase and view-phase reuses cached basics.
  EXPECT_EQ(pre_called, static_cast<int>(ScenePrepPipelineTest::kNodeCount));
  EXPECT_EQ(res_called, static_cast<int>(ScenePrepPipelineTest::kNodeCount));
  EXPECT_EQ(vis_called, static_cast<int>(ScenePrepPipelineTest::kNodeCount));
  EXPECT_EQ(prod_called, 0);
}

NOLINT_TEST_F(
  ScenePrepPipelineTest, PrepareView_RequiresBeginFrameCollectionForSameFrame)
{
  const auto pipeline = MakeContractTestPipeline();

  EXPECT_DEATH_IF_SUPPORTED(
    (void)pipeline->PrepareView(SceneRef(), ResolvedViewRef(),
      oxygen::frame::SequenceNumber {
        1,
      },
      StateRef()),
    "BeginFrameCollection");
}

NOLINT_TEST_F(ScenePrepPipelineTest,
  FinalizeView_RequiresTheSameStateThatPreparedTheCurrentView)
{
  const auto pipeline = MakeContractTestPipeline();
  sceneprep::ScenePrepState other_state(nullptr, nullptr, nullptr);

  pipeline->BeginFrameCollection(SceneRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  pipeline->PrepareView(SceneRef(), ResolvedViewRef(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());

  EXPECT_DEATH_IF_SUPPORTED(
    (void)pipeline->FinalizeView(other_state), "same ScenePrepState");
}

NOLINT_TEST_F(ScenePrepPipelineTest,
  CollectSingleView_RecordsStageFailuresAndSuppressesDuplicateLogs)
{
  auto pre = [](const sceneprep::ScenePrepContext& /*ctx*/,
               sceneprep::ScenePrepState& /*st*/,
               sceneprep::RenderItemProto& /*it*/) -> void {
    throw std::runtime_error("pre filter exploded");
  };

  using ConfigT
    = sceneprep::CollectionConfig<decltype(pre), void, void, void, void>;
  ConfigT cfg {
    .pre_filter = pre,
  };
  auto final_cfg = sceneprep::CreateStandardFinalizationConfig();
  const std::unique_ptr<sceneprep::ScenePrepPipeline> pipeline
    = std::make_unique<
      sceneprep::ScenePrepPipelineImpl<ConfigT, decltype(final_cfg)>>(
      cfg, final_cfg);

  oxygen::testing::ScopedLogCapture capture("ScenePrepFailureCapture",
    loguru::Verbosity_ERROR, [](const loguru::Message& message) -> bool {
      return std::string_view(message.message)
        .contains("ScenePrep view-phase stage");
    });

  pipeline->CollectSingleView(SceneRef(), ViewObserver(),
    oxygen::frame::SequenceNumber {
      1,
    },
    StateRef());
  pipeline->FinalizeView(StateRef());
  pipeline->CollectSingleView(SceneRef(), ViewObserver(),
    oxygen::frame::SequenceNumber {
      2,
    },
    StateRef());
  pipeline->FinalizeView(StateRef());

  const auto stats = pipeline->GetFailureStats();
  EXPECT_EQ(stats.total_failures, 6U);
  EXPECT_EQ(stats.logged_failures, 3U);
  EXPECT_EQ(stats.suppressed_failures, 3U);
  EXPECT_TRUE(StateRef().CollectedItems().empty());

  EXPECT_EQ(capture.Count("stage 'pre_filter'"), 3);
  EXPECT_EQ(capture.Count("RootA"), 1);
  EXPECT_EQ(capture.Count("RootB"), 1);
  EXPECT_EQ(capture.Count("ChildOfA"), 1);
}

} // namespace
