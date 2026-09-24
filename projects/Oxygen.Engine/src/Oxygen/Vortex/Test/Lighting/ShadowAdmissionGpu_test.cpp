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
    EXPECT_EQ(
      renderer_->GetLightingAllocationBudget()->Snapshot().rejected_requests,
      rejected.rejected_requests);
    RecordProperty("rejected_required_bytes", rejected.last_requested.get());
    RecordProperty("rejected_available_bytes", rejected.last_available.get());
  }
} // namespace
} // namespace oxygen::vortex::testing
