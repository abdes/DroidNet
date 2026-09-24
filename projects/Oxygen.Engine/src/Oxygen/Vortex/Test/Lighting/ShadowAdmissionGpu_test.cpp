//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>

namespace oxygen::vortex::testing {
namespace {
  class ShadowAdmissionGpuTest : public exposure::ExposureLightingGpuTest {
  protected:
    auto AdditionalCapabilities() const -> CapabilitySet override
    {
      return RendererCapabilityFamily::kShadowing;
    }
    auto SetUp() -> void override
    {
      initial_scene_capacity = 32U;
      ExposureLightingGpuTest::SetUp();
    }
    auto AddPoint(unsigned index) -> scene::SceneNode
    {
      auto node = scene->CreateNode("Admission point " + std::to_string(index));
      auto light = std::make_unique<scene::PointLight>();
      light->SetRange(3.0F);
      light->SetLuminousFluxLm(1.0F);
      light->Common().casts_shadows = true;
      light->Common().shadow.resolution_hint
        = scene::ShadowResolutionHint::kLow;
      EXPECT_TRUE(node.AttachLight(std::move(light)));
      return node;
    }
  };

  NOLINT_TEST_F(
    ShadowAdmissionGpuTest, FifthCubeAndNinthProjectedShadowRenderTogether)
  {
    for (unsigned index = 0U; index < 5U; ++index) {
      AddPoint(index);
    }
    for (unsigned index = 0U; index < 9U; ++index) {
      auto node = scene->CreateNode("Admission spot " + std::to_string(index));
      auto light = std::make_unique<scene::SpotLight>();
      light->SetRange(3.0F);
      light->SetLuminousFluxLm(1.0F);
      light->Common().casts_shadows = true;
      light->Common().shadow.resolution_hint
        = scene::ShadowResolutionHint::kLow;
      ASSERT_TRUE(node.AttachLight(std::move(light)));
      node.GetTransform().SetLocalRotation(
        glm::quat { 0.70710678F, 0.70710678F, 0, 0 });
    }
    unsigned inspected = 0U;
    auto first_surface = std::shared_ptr<const graphics::Texture> {};
    auto first_view = kInvalidViewId;
    probe->inspect = [&](const auto& ctx, const auto&, unsigned) -> void {
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      auto* shadows = RendererPublicationProbe::GetShadowService(*owner);
      const auto* data = shadows->InspectShadowData(ctx.current_view.view_id);
      ASSERT_NE(data, nullptr);
      EXPECT_EQ(data->cube_local_records.size(), 5U);
      EXPECT_EQ(data->projected_local_records.size(), 9U);
      EXPECT_EQ(data->local_shadow_references.size(), 14U);
      const auto surfaces
        = shadows->InspectPointShadowSurfaces(ctx.current_view.view_id);
      ASSERT_FALSE(surfaces.empty());
      const auto* surface = surfaces.front().get();
      ASSERT_NE(surface, nullptr);
      if (!first_surface) {
        first_surface = surface->shared_from_this();
        first_view = ctx.current_view.view_id;
      } else if (ctx.current_view.view_id != first_view) {
        EXPECT_NE(surface, first_surface.get());
      }
      ++inspected;
    };
    for (const bool forward : { false, true }) {
      surface_view_id = forward ? 101U : 100U;
      SetSurface(data::MaterialDomain::kOpaque);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 3U));
      EXPECT_GT(ReadFloatTexture(*probe->color).at(0).at(0), 1.0e-6F);
    }
    EXPECT_EQ(inspected, 6U);
    const auto memory = renderer_->GetLightingAllocationBudget()->Snapshot();
    EXPECT_GT(memory.allocated.get(), 0U);
    EXPECT_LE(memory.allocated, memory.limits.total);
    RecordProperty("lighting_allocation_bytes", memory.allocated.get());
  }

  NOLINT_TEST_F(ShadowAdmissionGpuTest, ContactDepthIsConditionalAndSelfDoesNotOcclude)
  {
    auto light_node = AddPoint(0U);
    bool expected_contact = false;
    probe->inspect = [&](const auto& ctx, const auto&, unsigned) {
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      auto* shadows = RendererPublicationProbe::GetShadowService(*owner);
      const auto* data = shadows->InspectShadowData(ctx.current_view.view_id);
      ASSERT_NE(data, nullptr);
      EXPECT_EQ(data->bindings.contact_enabled, expected_contact ? 1U : 0U);
      EXPECT_EQ(data->bindings.contact_depth_srv.IsValid(), expected_contact);
      if (expected_contact) {
        const auto index = data->bindings.contact_depth_srv.get();
        EXPECT_GE(index, bindless::generated::kTexturesShaderIndexBase);
        EXPECT_LT(index, bindless::generated::kTexturesShaderIndexBase
          + bindless::generated::kTexturesCapacity);
      }
    };
    for (const bool forward : { false, true }) {
      SetSurface(data::MaterialDomain::kOpaque);
      float baseline = 0.0F;
      for (const bool contact : { false, true, false }) {
        auto light = std::make_unique<scene::PointLight>(
          light_node.GetLightAs<scene::PointLight>()->get());
        light->Common().shadow.contact_shadows = contact;
        ASSERT_TRUE(light_node.ReplaceLight(std::move(light)));
        expected_contact = contact;
        ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 2U));
        const auto measured = ReadFloatTexture(*probe->color).at(0).at(0);
        ASSERT_GT(measured, 1.0e-6F);
        if (contact) {
          EXPECT_NEAR(measured, baseline, baseline * 0.005F + 2.0e-5F);
        } else {
          baseline = measured;
        }
      }
    }
    ASSERT_TRUE(light_node.EditLight<scene::PointLight>([](auto& light) {
      light.Common().affects_world = false;
    }));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    const auto empty = renderer_->InspectViewRenderStatus(ViewId { surface_view_id });
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->IsCaptureEligible(frame::SequenceNumber { sequence }));
  }

  NOLINT_TEST_F(ShadowAdmissionGpuTest,
    ContactOccluderUsesCasterMaskAndReceiverGateInBothPaths)
  {
    constexpr unsigned extent = 129U;
    constexpr unsigned center = extent / 2U * extent + extent / 2U;
    view.viewport.width = view.viewport.height = float(extent);
    camera.GetCameraAs<scene::PerspectiveCamera>()->get().SetViewport(view.viewport);
    auto output = CreateRegisteredTexture({ .width = extent, .height = extent,
      .format = Format::kRGBA32Float, .is_render_target = true,
      .initial_state = graphics::ResourceStates::kCommon });
    framebuffer = Backend().CreateFramebuffer(
      graphics::FramebufferDesc {}.AddColorAttachment(output));
    auto light = AddPoint(0U);
    ASSERT_TRUE(light.GetTransform().SetLocalPosition({ 1.0F, 0.0F, 0.0F }));
    ASSERT_TRUE(light.EditLight<scene::PointLight>([](auto& candidate) {
      // Suppress map occlusion to measure contact visibility independently.
      candidate.Common().shadow.bias = 1.0F;
    }));
    auto blocker = scene->CreateNode("Contact blocker");
    blocker.GetRenderable().SetGeometry(mesh_node.GetRenderable().GetGeometry());
    blocker.GetRenderable().SetMaterialOverride(0, 0, MakeEmissiveMaterial(0.0F));
    ASSERT_TRUE(blocker.GetTransform().SetLocalScale({ 0.05F, 0.05F, 0.05F }));
    ASSERT_TRUE(blocker.GetTransform().SetLocalPosition({ 0.09F, 0.0F, -0.8606F }));
    mesh_node.GetFlags()->get().SetLocalValue(scene::SceneNodeFlags::kCastsShadows, false);
    expected_draws = 2U;
    std::shared_ptr<const graphics::Texture> contact_depth;
    probe->inspect = [&](const auto& ctx, const auto&, unsigned) {
      const auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      contact_depth = RendererPublicationProbe::GetShadowService(*owner)
        ->InspectContactShadowSurface(ctx.current_view.view_id);
    };
    for (const bool forward : { false, true }) {
      SetSurface(data::MaterialDomain::kOpaque);
      mesh_node.GetFlags()->get().SetLocalValue(scene::SceneNodeFlags::kReceivesShadows, true);
      blocker.GetFlags()->get().SetLocalValue(scene::SceneNodeFlags::kCastsShadows, true);
      ASSERT_TRUE(light.EditLight<scene::PointLight>([](auto& candidate) {
        candidate.Common().shadow.contact_shadows = false;
      }));
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 2U));
      const auto baseline = ReadFloatTexture(*probe->color).at(center).at(0);
      ASSERT_GT(baseline, 1.0e-6F);
      ASSERT_TRUE(light.EditLight<scene::PointLight>([](auto& candidate) {
        candidate.Common().shadow.contact_shadows = true;
      }));
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
      ASSERT_NE(contact_depth, nullptr);
      EXPECT_EQ(contact_depth->GetDescriptor().format, Format::kDepth32);
      EXPECT_EQ(contact_depth->GetDescriptor().width, extent);
      EXPECT_EQ(contact_depth->GetDescriptor().height, extent);
      EXPECT_LT(ReadFloatTexture(*probe->color).at(center).at(0), baseline * 0.8F);
      blocker.GetFlags()->get().SetLocalValue(scene::SceneNodeFlags::kCastsShadows, false);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
      EXPECT_NEAR(ReadFloatTexture(*probe->color).at(center).at(0), baseline, baseline * 0.005F);
      blocker.GetFlags()->get().SetLocalValue(scene::SceneNodeFlags::kCastsShadows, true);
      mesh_node.GetFlags()->get().SetLocalValue(scene::SceneNodeFlags::kReceivesShadows, false);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
      EXPECT_NEAR(ReadFloatTexture(*probe->color).at(center).at(0), baseline, baseline * 0.005F);
    }
  }

  class BoundedShadowAdmissionGpuTest : public ShadowAdmissionGpuTest {
  protected:
    auto ConfigureRenderer(RendererConfig& config) const -> void override
    {
      config.lighting_allocation_limit_bytes = 64ULL * 1024ULL * 1024ULL;
      config.lighting_compact_index_limit_bytes = 8ULL * 1024ULL * 1024ULL;
    }
  };

  NOLINT_TEST_F(BoundedShadowAdmissionGpuTest,
    FailedGrowthRejectsWholeViewAndSmallerRequestRecovers)
  {
    for (unsigned index = 0U; index < 4U; ++index) {
      AddPoint(index);
    }
    SetSurface(data::MaterialDomain::kOpaque);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 3U));
    const auto before = renderer_->GetLightingAllocationBudget()->Snapshot();
    auto extras = std::vector<scene::SceneNode> {};
    for (unsigned index = 4U; index < 11U; ++index) {
      extras.push_back(AddPoint(index));
    }
    ASSERT_NO_FATAL_FAILURE(
      RenderSurface(false, 0.0F, 1U, ExpectedViewOutcome::kRejected));
    const auto failure = renderer_->InspectViewRenderStatus(ViewId { surface_view_id });
    ASSERT_TRUE(failure);
    EXPECT_EQ(failure->state, ViewRenderState::kFailed);
    EXPECT_EQ(failure->failure, ViewRenderFailure::kLighting);
    EXPECT_FALSE(failure->IsCaptureEligible(frame::SequenceNumber { sequence }));
    const auto rejected = renderer_->GetLightingAllocationBudget()->Snapshot();
    EXPECT_GT(rejected.rejected_requests, before.rejected_requests);
    EXPECT_GT(rejected.last_requested, rejected.last_available);
    EXPECT_LE(rejected.allocated, rejected.limits.total);
    for (auto& extra : extras) {
      auto disabled = std::make_unique<scene::PointLight>();
      disabled->Common().affects_world = false;
      ASSERT_TRUE(extra.ReplaceLight(std::move(disabled)));
    }
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    EXPECT_GT(ReadFloatTexture(*probe->color).at(0).at(0), 1.0e-6F);
    const auto recovery = renderer_->InspectViewRenderStatus(ViewId { surface_view_id });
    ASSERT_TRUE(recovery);
    EXPECT_TRUE(recovery->IsCaptureEligible(frame::SequenceNumber { sequence }));
    EXPECT_EQ(
      renderer_->GetLightingAllocationBudget()->Snapshot().rejected_requests,
      rejected.rejected_requests);
    RecordProperty("rejected_required_bytes", rejected.last_requested.get());
    RecordProperty("rejected_available_bytes", rejected.last_available.get());
  }
} // namespace
} // namespace oxygen::vortex::testing
