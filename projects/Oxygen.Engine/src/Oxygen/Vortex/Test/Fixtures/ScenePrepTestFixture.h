//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/gtc/matrix_access.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemProto.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepContext.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepState.h>

namespace oxygen::vortex::sceneprep::testing {

namespace scene = oxygen::scene;
namespace frame = oxygen::frame;

//! Shared test fixture for ScenePrep unit tests.
/*!
 A compact fixture that centralizes common scene/node/proto setup and
  provides mesh/geometry builder helpers and view configuration helpers.
*/
class ScenePrepTestFixture : public ::testing::Test {
protected:
  static constexpr size_t kDefaultSceneCapacity = 1024;

  auto SetUp() -> void override
  {
    scene_ = std::make_shared<scene::Scene>("TestScene", kDefaultSceneCapacity);
    node_ = scene_->CreateNode("TestNode");

    // non-trivial transform to ensure world-sphere is meaningful
    node_.GetTransform().SetLocalTransform(
      glm::vec3(0.2F), glm::quat(0.6F, 0.0F, 0.0F, 0.0F), glm::vec3(3.0F));
    scene_->Update();

    // default rendering flags used by tests
    Flags().SetFlag(scene::SceneNodeFlags::kCastsShadows,
      scene::SceneFlag {}.SetEffectiveValueBit(true));
    Flags().SetFlag(scene::SceneNodeFlags::kReceivesShadows,
      scene::SceneFlag {}.SetEffectiveValueBit(true));

    // Ensure node has a default geometry before constructing the proto
    AddDefaultGeometry();
    const auto node_impl = node_.GetImpl();
    if (!node_impl.has_value()) {
      FAIL() << "Scene node has no implementation";
    }
    proto_.emplace(node_impl->get());

    // By default, do not emplace ctx_; tests can call EmplaceContext* helpers
    // Create a default (null-backed) ScenePrepState. Tests can override
    // CreateScenePrepState() to provide custom resource managers.
    state_ = CreateScenePrepState();
  }

  auto TearDown() -> void override
  {
    // Destroy state before tearing down the scene to ensure resource
    // uploaders or binders that reference the scene/graphics outlive the
    // state.
    state_.reset();
    scene_.reset();
  }

  // Use the fixture's stored view_ to construct the context (tests call
  // this overload frequently).
  auto EmplaceContextWithView() -> void
  {
    oxygen::observer_ptr<const oxygen::ResolvedView> rv {
      nullptr,
    };
    if (view_) {
      rv = oxygen::observer_ptr<const oxygen::ResolvedView>(view_.get());
    }
    ctx_.emplace(
      frame::SequenceNumber {
        0,
      },
      rv, *scene_);
  }

  // ReSharper disable once CppMemberFunctionMayBeConst
  auto UpdateScene() -> void { scene_->Update(); }

  // Helpers
  auto SetView(const oxygen::ResolvedView::Params& rp) -> void
  {
    view_ = std::make_shared<oxygen::ResolvedView>(rp);
    EmplaceContextWithView();
  }

  auto SeedVisibilityAndTransform() -> void
  {
    Proto().SetVisible();
    Proto().SetWorldTransform(WorldMatrix());
  }

  auto ConfigureView(const glm::vec3 cam_pos, const float viewport_height)
    -> void
  {
    oxygen::ResolvedView::Params p {};
    p.view_matrix = glm::mat4(1.0F);
    p.proj_matrix = glm::mat4(1.0F);
    p.view_config.viewport = {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 0.0F,
      .height = viewport_height,
    };
    p.camera_position = cam_pos;
    p.near_plane = 0.1F;
    p.far_plane = 1000.0F;
    SetView(p);
  }

  auto ConfigurePerspectiveView(const glm::vec3 eye, const glm::vec3 center)
    -> void
  {
    oxygen::ResolvedView::Params p {};
    p.view_matrix = glm::lookAt(eye, center,
      glm::vec3 {
        0,
        1,
        0,
      });
    p.proj_matrix = glm::perspective(glm::radians(60.0F), 1.0F, 0.1F, 1000.0F);
    p.view_config.viewport = {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 1000.0F,
      .height = 1000.0F,
    };
    p.camera_position = eye;
    p.near_plane = 0.1F;
    p.far_plane = 1000.0F;
    SetView(p);
  }

  auto SetGeometry(const std::shared_ptr<data::GeometryAsset>& geometry) -> void
  {
    Node().GetRenderable().SetGeometry(geometry);
    Proto().SetGeometry(geometry);
  }

  auto MarkDropped() -> void { Proto().MarkDropped(); }
  auto MarkVisible() -> void { Proto().SetVisible(); }

  //! Factory hook for derived fixtures to provide a custom ScenePrepState.
  /*! Default implementation returns a ScenePrepState with null resource
      managers (suitable for light-weight tests). Override to supply
      uploaders/binders needed by integration tests. */
  virtual auto CreateScenePrepState() -> std::unique_ptr<ScenePrepState>
  {
    return std::make_unique<ScenePrepState>(nullptr, nullptr, nullptr);
  }

  // Accessors
  // ReSharper disable CppMemberFunctionMayBeConst
  [[nodiscard]] auto Context() -> ScenePrepContext&
  {
    if (!ctx_.has_value()) {
      throw std::logic_error("ScenePrep context has not been configured");
    }
    return *ctx_;
  }
  //! Access the ScenePrepState owned by the fixture.
  [[nodiscard]] auto State() -> ScenePrepState& { return *state_; }
  [[nodiscard]] auto Proto() -> RenderItemProto&
  {
    if (!proto_.has_value()) {
      throw std::logic_error("ScenePrep prototype has not been initialized");
    }
    return *proto_;
  }
  [[nodiscard]] auto Node() -> scene::SceneNode& { return node_; }
  [[nodiscard]] auto Flags() -> scene::SceneNode::Flags&
  {
    const auto flags = node_.GetFlags();
    if (!flags.has_value()) {
      throw std::logic_error("ScenePrep node has no flags");
    }
    return flags->get();
  }
  [[nodiscard]] auto WorldMatrix() const -> glm::mat4
  {
    const auto matrix = node_.GetTransform().GetWorldMatrix();
    if (!matrix.has_value()) {
      throw std::logic_error("ScenePrep node has no world matrix");
    }
    return *matrix;
  }
  // ReSharper restore CppMemberFunctionMayBeConst

  // Minimal default geometry used in many tests
  auto AddDefaultGeometry() -> void
  {
    using data::AssetKey;
    using data::GeometryAsset;
    using data::MaterialAsset;
    using data::Mesh;
    using data::MeshBuilder;
    using data::Vertex;
    namespace pak = data::pak;
    std::vector<Vertex> vertices(3);
    std::vector<uint32_t> idx = {
      0,
      1,
      2,
    };
    const auto mat = MaterialAsset::CreateDefault();
    const std::shared_ptr mesh = MeshBuilder()
                                   .WithVertices(vertices)
                                   .WithIndices(idx)
                                   .BeginSubMesh("s", mat)
                                   .WithMeshView({
                                     .first_index = 0,
                                     .index_count = 3,
                                     .first_vertex = 0,
                                     .vertex_count = 3,
                                   })
                                   .EndSubMesh()
                                   .Build();

    pak::geometry::GeometryAssetDesc desc {};
    desc.lod_count = 1;
    std::vector<std::shared_ptr<Mesh>> lods;
    lods.push_back(mesh);
    const auto geometry
      = std::make_shared<GeometryAsset>(AssetKey {}, desc, std::move(lods));

    Node().GetRenderable().SetGeometry(geometry);
  }

  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode node_;
  // Provide a default (empty) ResolvedView so tests can emplace a context in
  // SetUp without explicitly calling a Configure* helper.
  std::shared_ptr<oxygen::ResolvedView> view_
    = std::make_shared<oxygen::ResolvedView>(oxygen::ResolvedView::Params {
      .near_plane = 0.1F,
      .far_plane = 1000.0F,
    });
  std::optional<ScenePrepContext> ctx_;
  std::unique_ptr<ScenePrepState> state_;
  std::optional<RenderItemProto> proto_;
};

} // namespace oxygen::vortex::sceneprep::testing
