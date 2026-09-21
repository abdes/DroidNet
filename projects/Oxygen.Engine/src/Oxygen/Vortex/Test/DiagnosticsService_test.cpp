//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// assertions; setup loops stay within their sized containers.

#include <memory>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::frame::SequenceNumber;
using oxygen::graphics::QueueRole;
using oxygen::vortex::CapabilitySet;
using oxygen::vortex::DiagnosticsConfig;
using oxygen::vortex::DiagnosticsDebugPath;
using oxygen::vortex::DiagnosticsFeature;
using oxygen::vortex::DiagnosticsIssue;
using oxygen::vortex::DiagnosticsPassKind;
using oxygen::vortex::DiagnosticsPassRecord;
using oxygen::vortex::DiagnosticsProductRecord;
using oxygen::vortex::DiagnosticsService;
using oxygen::vortex::DiagnosticsSeverity;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::ShaderDebugMode;
using oxygen::vortex::testing::FakeGraphics;

constexpr auto kDiagnosticsCapability
  = RendererCapabilityFamily::kDiagnosticsAndProfiling;

TEST(DiagnosticsServiceTest, ProductionPrecisionDefaultAndControlRevisions)
{
  using oxygen::vortex::HdrPrecisionControl;
  DiagnosticsService service(kDiagnosticsCapability);
  EXPECT_EQ(service.GetHdrPrecisionControl(), HdrPrecisionControl::kProduction);
  EXPECT_EQ(service.GetHdrPrecisionControlRevision(), 0U);
  service.SetHdrPrecisionControl(HdrPrecisionControl::kProduction);
  EXPECT_EQ(service.GetHdrPrecisionControlRevision(), 0U);
  service.SetHdrPrecisionControl(HdrPrecisionControl::kQualified);
  EXPECT_EQ(service.GetHdrPrecisionControlRevision(), 1U);
  service.SetHdrFp32ReferenceEnabled(true);
  EXPECT_EQ(
    service.GetHdrPrecisionControl(), HdrPrecisionControl::kFp32Reference);
  EXPECT_EQ(service.GetHdrPrecisionControlRevision(), 2U);
  service.SetHdrFp32ReferenceEnabled(false);
  EXPECT_EQ(service.GetHdrPrecisionControl(), HdrPrecisionControl::kProduction);
  EXPECT_EQ(service.GetHdrPrecisionControlRevision(), 3U);
}

auto MakeConfig(FakeGraphics& graphics) -> RendererConfig
{
  auto config = RendererConfig {};
  config.upload_queue_key = graphics.QueueKeyFor(QueueRole::kGraphics).get();
  return config;
}

auto MakeRenderer(const std::shared_ptr<FakeGraphics>& graphics,
  const CapabilitySet capabilities) -> std::shared_ptr<Renderer>
{
  return {
    new Renderer(
      std::weak_ptr<Graphics>(graphics), MakeConfig(*graphics), capabilities),
    [](Renderer* renderer) -> void {
      if (renderer != nullptr) {
        renderer->OnShutdown();
        std::default_delete<Renderer> {}(renderer);
      }
    },
  };
}

NOLINT_TEST(DiagnosticsTypesTest, FeatureFlagsUseStableStringOrder)
{
  const auto features = DiagnosticsFeature::kGpuTimeline
    | DiagnosticsFeature::kFrameLedger | DiagnosticsFeature::kCaptureManifest;

  EXPECT_EQ(oxygen::vortex::to_string(features),
    "FrameLedger | GpuTimeline | CaptureManifest");
  EXPECT_EQ(oxygen::vortex::to_string(DiagnosticsFeature::kNone), "None");
}

NOLINT_TEST(DiagnosticsTypesTest, EnumStringsCoverPublicDiagnosticsEnums)
{
  EXPECT_EQ(
    oxygen::vortex::to_string(DiagnosticsSeverity::kWarning), "Warning");
  EXPECT_EQ(
    oxygen::vortex::to_string(DiagnosticsPassKind::kCompute), "Compute");
  EXPECT_EQ(
    oxygen::vortex::to_string(DiagnosticsDebugPath::kForwardMeshVariant),
    "ForwardMeshVariant");
}

NOLINT_TEST(DiagnosticsServiceTest, CapabilityClampDisablesRequestedFeatures)
{
  auto service = DiagnosticsService {
    RendererCapabilityFamily::kNone,
    DiagnosticsConfig {
      .default_features = DiagnosticsFeature::kFrameLedger
        | DiagnosticsFeature::kShaderDebugModes,
    },
  };

  EXPECT_EQ(service.GetRequestedFeatures(),
    DiagnosticsFeature::kFrameLedger | DiagnosticsFeature::kShaderDebugModes);
  EXPECT_EQ(service.GetEnabledFeatures(), DiagnosticsFeature::kNone);

  service.SetRendererCapabilities(kDiagnosticsCapability);

  EXPECT_EQ(service.GetEnabledFeatures(),
    DiagnosticsFeature::kFrameLedger | DiagnosticsFeature::kShaderDebugModes);
}

NOLINT_TEST(DiagnosticsServiceTest, FrameLedgerRecordsFactsWhenEnabled)
{
  auto service = DiagnosticsService {
    kDiagnosticsCapability,
    DiagnosticsConfig {
      .default_features = DiagnosticsFeature::kFrameLedger,
    },
  };

  service.SetShaderDebugMode(ShaderDebugMode::kDirectionalShadowMask);
  service.BeginFrame(SequenceNumber {
    42U,
  });
  service.RecordPass(DiagnosticsPassRecord {
    .name = "Stage8.ShadowDepth.Directional",
    .kind = DiagnosticsPassKind::kGraphics,
    .executed = true,
    .inputs = {},
    .outputs = { "Vortex.DirectionalShadowSurface" },
    .missing_inputs = {},
    .gpu_duration_ms = {},
  });
  service.RecordProduct(DiagnosticsProductRecord {
    .name = "Vortex.DirectionalShadowSurface",
    .producer_pass = "Stage8.ShadowDepth.Directional",
    .resource_name = {},
    .descriptor = {},
    .published = true,
    .valid = true,
  });
  service.ReportIssue(DiagnosticsIssue {
    .severity = DiagnosticsSeverity::kWarning,
    .code = "debug-mode.missing-product",
    .message = "test issue",
    .view_name = {},
    .pass_name = {},
    .product_name = {},
  });
  service.EndFrame();

  const auto snapshot = service.GetLatestSnapshot();
  EXPECT_EQ(snapshot.frame_index,
    (SequenceNumber {
      42U,
    }));
  EXPECT_EQ(
    snapshot.active_shader_debug_mode, ShaderDebugMode::kDirectionalShadowMask);
  ASSERT_EQ(snapshot.passes.size(), 1U);
  EXPECT_EQ(snapshot.passes.at(0).name, "Stage8.ShadowDepth.Directional");
  ASSERT_EQ(snapshot.products.size(), 1U);
  EXPECT_EQ(snapshot.products.at(0).name, "Vortex.DirectionalShadowSurface");
  ASSERT_EQ(snapshot.issues.size(), 1U);
  EXPECT_EQ(snapshot.issues.at(0).code, "debug-mode.missing-product");
}

NOLINT_TEST(DiagnosticsServiceTest, DisabledLedgerDoesNotAccumulateRecords)
{
  auto service = DiagnosticsService {
    kDiagnosticsCapability,
    DiagnosticsConfig {
      .default_features = DiagnosticsFeature::kNone,
    },
  };

  service.BeginFrame(SequenceNumber {
    7U,
  });
  service.RecordPass(DiagnosticsPassRecord {
    .name = "Skipped",
    .inputs = {},
    .outputs = {},
    .missing_inputs = {},
    .gpu_duration_ms = {},
  });
  service.RecordProduct(DiagnosticsProductRecord {
    .name = "Skipped",
    .producer_pass = {},
    .resource_name = {},
    .descriptor = {},
  });
  service.ReportIssue(DiagnosticsIssue {
    .code = "diag.feature-unavailable",
    .message = {},
    .view_name = {},
    .pass_name = {},
    .product_name = {},
  });
  service.EndFrame();

  const auto snapshot = service.GetLatestSnapshot();
  EXPECT_EQ(snapshot.frame_index,
    (SequenceNumber {
      7U,
    }));
  EXPECT_TRUE(snapshot.passes.empty());
  EXPECT_TRUE(snapshot.products.empty());
  EXPECT_TRUE(snapshot.issues.empty());
}

NOLINT_TEST(DiagnosticsServiceTest, RendererOwnsServiceAndForwardsDebugMode)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics, kDiagnosticsCapability);

  EXPECT_EQ(renderer->GetDiagnosticsService().GetRendererCapabilities(),
    kDiagnosticsCapability);

  renderer->SetShaderDebugMode(ShaderDebugMode::kSceneDepthLinear);

  EXPECT_EQ(renderer->GetShaderDebugMode(), ShaderDebugMode::kSceneDepthLinear);
  EXPECT_EQ(renderer->GetDiagnosticsService().GetShaderDebugMode(),
    ShaderDebugMode::kSceneDepthLinear);
}

NOLINT_TEST(DiagnosticsServiceTest, ExposesShaderDebugModeRegistry)
{
  auto service = DiagnosticsService {
    kDiagnosticsCapability,
    DiagnosticsConfig {
      .default_features = DiagnosticsFeature::kShaderDebugModes,
    },
  };

  EXPECT_FALSE(service.EnumerateShaderDebugModes().empty());
  const auto resolved = service.FindShaderDebugMode("directional-shadow-mask");
  if (!resolved.has_value()) {
    FAIL() << "Expected resolved to have a value";
  }
  EXPECT_EQ(*resolved, ShaderDebugMode::kDirectionalShadowMask);
  EXPECT_FALSE(service.FindShaderDebugMode("missing-mode").has_value());
}

NOLINT_TEST(DiagnosticsServiceTest, RendererOwnedGpuTimelineCanBeEnabled)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  const auto renderer = MakeRenderer(graphics, kDiagnosticsCapability);
  auto& service = renderer->GetDiagnosticsService();

  service.SetEnabledFeatures(
    DiagnosticsFeature::kFrameLedger | DiagnosticsFeature::kGpuTimeline);
  service.SetGpuTimelineRetainLatestFrame(true);
  service.SetGpuTimelineMaxScopesPerFrame(1U);
  service.SetGpuTimelineEnabled(true);

  EXPECT_TRUE(service.IsGpuTimelineEnabled());
  EXPECT_TRUE(HasAllFeatures(
    service.GetEnabledFeatures(), DiagnosticsFeature::kGpuTimeline));
}

} // namespace
