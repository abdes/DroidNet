//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <optional>

#include <algorithm>

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/CollectionConfig.h>
#include <Oxygen/Renderer/ScenePrep/FinalizationConfig.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Sceneprep/ScenePrepHelpers.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>

#ifdef OXYGEN_ENGINE_TESTING

namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

#endif

namespace {

namespace sceneprep = oxygen::engine::sceneprep;

using oxygen::observer_ptr;
using oxygen::engine::upload::DefaultUploadPolicy;
using oxygen::engine::upload::InlineTransfersCoordinator;
using oxygen::engine::upload::StagingProvider;
using oxygen::engine::upload::UploadCoordinator;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::SingleQueueStrategy;
using oxygen::renderer::LightManager;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

//=== ScenePrep + LightManager integration tests ===--------------------------//

//! Validates ScenePrep frame-phase traversal feeds LightManager.
/*! Regression guard:

    ScenePrep traverses the full scene node table during Frame-phase. Even if a
    node has no renderable (and is skipped for RenderItemProto construction),
    it must still be offered to LightManager so light-only nodes are collected.
*/
NOLINT_TEST(ScenePrepLightManagerIntegration,
  Collect_FramePhase_CollectsLightFromNonRenderableNode)
{
  // Arrange: backend fakes required by LightManager.
  const auto gfx = std::make_shared<FakeGraphics>();
  gfx->CreateCommandQueues(SingleQueueStrategy());

  auto uploader = std::make_unique<UploadCoordinator>(
    observer_ptr { gfx.get() }, DefaultUploadPolicy());
  const auto staging_provider
    = uploader->CreateRingBufferStaging(oxygen::frame::SlotCount { 1 }, 256u);
  auto inline_transfers
    = std::make_unique<InlineTransfersCoordinator>(observer_ptr { gfx.get() });

  auto light_manager = std::make_unique<LightManager>(
    observer_ptr { gfx.get() }, observer_ptr { staging_provider.get() },
    observer_ptr { inline_transfers.get() });

  // ScenePrepState owns the LightManager.
  auto state = std::make_unique<sceneprep::ScenePrepState>(
    std::unique_ptr<oxygen::renderer::resources::GeometryUploader> {},
    std::unique_ptr<oxygen::renderer::resources::TransformUploader> {},
    std::unique_ptr<oxygen::renderer::resources::MaterialBinder> {},
    std::unique_ptr<oxygen::renderer::resources::DrawMetadataEmitter> {},
    std::move(light_manager));

  const auto scene = std::make_shared<Scene>("ScenePrepLightManagerScene", 64);

  // Light-only node: intentionally has no renderable component.
  const auto light_flags = SceneNode::Flags {}
                             .SetFlag(SceneNodeFlags::kVisible,
                               SceneFlag {}.SetEffectiveValueBit(true))
                             .SetFlag(SceneNodeFlags::kCastsShadows,
                               SceneFlag {}.SetEffectiveValueBit(true));
  auto light_node = scene->CreateNode("LightOnly", light_flags);
  ASSERT_TRUE(light_node.IsValid());

  const auto light_impl = light_node.GetImpl();
  ASSERT_TRUE(light_impl.has_value());
  light_impl->get().AddComponent<oxygen::scene::DirectionalLight>();

  // A renderable node so the pipeline does real extraction work too.
  auto renderable = scene->CreateNode("Renderable");
  ASSERT_TRUE(renderable.IsValid());
  const auto renderable_impl_opt = renderable.GetImpl();
  ASSERT_TRUE(renderable_impl_opt.has_value());
  const auto* renderable_impl = &renderable_impl_opt->get();
  renderable.GetRenderable().SetGeometry(
    oxygen::engine::sceneprep::testing::MakeGeometryWithLods(
      1, glm::vec3(-1.0f), glm::vec3(1.0f)));

  scene->Update();

  // LightManager must be started before the scene traversal.
  auto* lm = state->GetLightManager().get();
  ASSERT_NE(lm, nullptr);
  lm->OnFrameStart(RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  const auto cfg = sceneprep::CreateBasicCollectionConfig();
  const auto final_cfg = sceneprep::CreateStandardFinalizationConfig();
  const std::unique_ptr<sceneprep::ScenePrepPipeline> pipeline
    = std::make_unique<
      sceneprep::ScenePrepPipelineImpl<decltype(cfg), decltype(final_cfg)>>(
      cfg, final_cfg);

  // Act: run frame-phase (no view).
  pipeline->Collect(*scene, std::nullopt, SequenceNumber { 1 }, *state,
    /*reset_state=*/true);

  // Assert: light-only node was still offered to LightManager.
  EXPECT_EQ(lm->GetDirectionalLights().size(), 1);

  // Assert: frame-phase still processed renderable nodes and cached them for
  // view-phase iteration.
  const auto& filtered_nodes = state->GetFilteredSceneNodes();
  ASSERT_FALSE(filtered_nodes.empty());
  EXPECT_NE(
    std::find(filtered_nodes.begin(), filtered_nodes.end(), renderable_impl),
    filtered_nodes.end());

  // Assert: non-renderable nodes are not cached in the filtered list.
  EXPECT_EQ(
    std::find(filtered_nodes.begin(), filtered_nodes.end(), &light_impl->get()),
    filtered_nodes.end());
}

} // namespace
