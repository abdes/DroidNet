//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <memory>

#include <Oxygen/Console/Console.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/ViewFeatureProfile.h>

#include "Fakes/Graphics.h"

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::console::Console;
using oxygen::console::ExecutionStatus;
using oxygen::graphics::QueueRole;
using oxygen::vortex::CapabilitySet;
using oxygen::vortex::kPhase1DefaultRuntimeCapabilityFamilies;
using oxygen::vortex::PipelineCapabilityRequirements;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::ResolveViewFeatureProfileSpec;
using oxygen::vortex::ValidateCapabilityRequirements;
using oxygen::vortex::testing::FakeGraphics;

class RendererCapabilityBindingTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  }

  [[nodiscard]] auto MakeRenderer(const CapabilitySet capabilities)
    -> std::shared_ptr<Renderer>
  {
    auto config = RendererConfig {};
    config.upload_queue_key
      = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
    return std::shared_ptr<Renderer>(
      new Renderer(
        std::weak_ptr<Graphics>(graphics_), std::move(config), capabilities),
      [](Renderer* renderer) {
        if (renderer != nullptr) {
          renderer->OnShutdown();
          delete renderer;
        }
      });
  }

  [[nodiscard]] auto MakeDefaultRenderer() -> std::shared_ptr<Renderer>
  {
    auto config = RendererConfig {};
    config.upload_queue_key
      = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
    return std::shared_ptr<Renderer>(
      new Renderer(std::weak_ptr<Graphics>(graphics_), std::move(config)),
      [](Renderer* renderer) {
        if (renderer != nullptr) {
          renderer->OnShutdown();
          delete renderer;
        }
      });
  }

  std::shared_ptr<FakeGraphics> graphics_;
};

NOLINT_TEST(RendererCapabilityTest,
  DeferredShadingVocabularyExistsWithoutEnteringPhase1Defaults)
{
  EXPECT_EQ(RendererCapabilityFamily::kNone,
    oxygen::vortex::MissingCapabilities(RendererCapabilityFamily::kAll,
      RendererCapabilityFamily::kDeferredShading));
  EXPECT_EQ(RendererCapabilityFamily::kDeferredShading,
    oxygen::vortex::MissingCapabilities(kPhase1DefaultRuntimeCapabilityFamilies,
      RendererCapabilityFamily::kDeferredShading));
}

NOLINT_TEST(RendererCapabilityTest, Phase1DefaultsStaySubstrateOnly)
{
  const auto expected = RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kGpuUploadAndAssetBinding
    | RendererCapabilityFamily::kFinalOutputComposition;

  EXPECT_EQ(kPhase1DefaultRuntimeCapabilityFamilies, expected);
  EXPECT_EQ(oxygen::vortex::to_string(kPhase1DefaultRuntimeCapabilityFamilies),
    "ScenePreparation | GpuUploadAndAssetBinding | FinalOutputComposition");
}

NOLINT_TEST(RendererCapabilityTest, ToStringKeepsDeferredShadingInStableOrder)
{
  const auto families = RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kFinalOutputComposition
    | RendererCapabilityFamily::kDeferredShading;

  EXPECT_EQ(oxygen::vortex::to_string(families),
    "ScenePreparation | FinalOutputComposition | DeferredShading");
}

NOLINT_TEST(RendererCapabilityTest, ValidationReportsMissingFamilies)
{
  const auto available = RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kGpuUploadAndAssetBinding
    | RendererCapabilityFamily::kFinalOutputComposition;
  const auto requirements = PipelineCapabilityRequirements {
    .required = RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kFinalOutputComposition,
    .optional = RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kDiagnosticsAndProfiling,
  };

  const auto validation
    = ValidateCapabilityRequirements(available, requirements);

  EXPECT_TRUE(validation.Ok());
  EXPECT_EQ(validation.available, available);
  EXPECT_EQ(validation.missing_required, RendererCapabilityFamily::kNone);
  EXPECT_EQ(validation.missing_optional,
    RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kDiagnosticsAndProfiling);
}

NOLINT_TEST(RendererCapabilityTest, ViewFeatureProfilesResolveStableMasks)
{
  using Profile = oxygen::vortex::CompositionView::ViewFeatureProfile;
  using Mask = oxygen::vortex::CompositionView::ViewFeatureMask;

  const auto depth_only = ResolveViewFeatureProfileSpec(Profile::kDepthOnly);
  EXPECT_TRUE(depth_only.depth_only);
  EXPECT_FALSE(depth_only.feature_mask.Has(Mask::kSceneLighting));
  EXPECT_FALSE(depth_only.feature_mask.Has(Mask::kShadows));
  EXPECT_FALSE(depth_only.requires_color_output);

  const auto no_environment
    = ResolveViewFeatureProfileSpec(Profile::kNoEnvironment);
  EXPECT_TRUE(no_environment.feature_mask.Has(Mask::kSceneLighting));
  EXPECT_TRUE(no_environment.feature_mask.Has(Mask::kShadows));
  EXPECT_FALSE(no_environment.feature_mask.Has(Mask::kEnvironment));
  EXPECT_FALSE(no_environment.feature_mask.Has(Mask::kVolumetrics));

  const auto no_shadowing
    = ResolveViewFeatureProfileSpec(Profile::kNoShadowing);
  EXPECT_TRUE(no_shadowing.feature_mask.Has(Mask::kSceneLighting));
  EXPECT_FALSE(no_shadowing.feature_mask.Has(Mask::kShadows));
  EXPECT_TRUE(no_shadowing.feature_mask.Has(Mask::kEnvironment));

  const auto diagnostics_only
    = ResolveViewFeatureProfileSpec(Profile::kDiagnosticsOnly);
  EXPECT_TRUE(diagnostics_only.diagnostics_only);
  EXPECT_TRUE(diagnostics_only.feature_mask.Has(Mask::kDiagnostics));
  EXPECT_FALSE(diagnostics_only.feature_mask.Has(Mask::kSceneLighting));
}

NOLINT_TEST(RendererCapabilityTest, FeatureProfileCapabilitiesReportMissing)
{
  using Profile = oxygen::vortex::CompositionView::ViewFeatureProfile;

  const auto shadow_only = ResolveViewFeatureProfileSpec(Profile::kShadowOnly);
  const auto available = RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kDeferredShading
    | RendererCapabilityFamily::kLightingData;

  const auto validation = ValidateCapabilityRequirements(
    available, shadow_only.capability_requirements);

  EXPECT_FALSE(validation.Ok());
  EXPECT_EQ(validation.missing_required, RendererCapabilityFamily::kShadowing);
}

NOLINT_TEST_F(
  RendererCapabilityBindingTest, RendererUsesPhase1DefaultCapabilitiesByDefault)
{
  const auto renderer = MakeDefaultRenderer();
  EXPECT_EQ(
    renderer->GetCapabilityFamilies(), kPhase1DefaultRuntimeCapabilityFamilies);
}

NOLINT_TEST_F(
  RendererCapabilityBindingTest, OcclusionConsoleCVarsDefaultOffAndClamp)
{
  const auto renderer = MakeRenderer(
    RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kDeferredShading);
  auto console = Console {};
  renderer->RegisterConsoleBindings(oxygen::observer_ptr { &console });

  EXPECT_FALSE(renderer->GetOcclusionEnabled());
  EXPECT_EQ(renderer->GetOcclusionMaxCandidateCount(), 256U * 256U);

  EXPECT_EQ(
    console.Execute("vtx.occlusion.enable true").status, ExecutionStatus::kOk);
  EXPECT_TRUE(renderer->GetOcclusionEnabled());

  EXPECT_EQ(console.Execute("vtx.occlusion.max_candidate_count 999999").status,
    ExecutionStatus::kOk);
  EXPECT_EQ(renderer->GetOcclusionMaxCandidateCount(), 256U * 256U);

  EXPECT_EQ(console.Execute("vtx.occlusion.max_candidate_count 0").status,
    ExecutionStatus::kOk);
  EXPECT_EQ(renderer->GetOcclusionMaxCandidateCount(), 1U);
}

} // namespace
