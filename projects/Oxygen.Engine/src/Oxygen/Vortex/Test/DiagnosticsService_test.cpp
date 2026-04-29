//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <memory>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>
#include <Oxygen/Vortex/Renderer.h>
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
using oxygen::vortex::internal::GpuTimelineProfiler;
using oxygen::vortex::testing::FakeGraphics;

constexpr auto kDiagnosticsCapability
  = RendererCapabilityFamily::kDiagnosticsAndProfiling;

auto MakeConfig(FakeGraphics& graphics) -> RendererConfig
{
  auto config = RendererConfig {};
  config.upload_queue_key
    = graphics.QueueKeyFor(QueueRole::kGraphics).get();
  return config;
}

auto MakeRenderer(std::shared_ptr<FakeGraphics> graphics,
  const CapabilitySet capabilities) -> std::shared_ptr<Renderer>
{
  return std::shared_ptr<Renderer>(
    new Renderer(std::weak_ptr<Graphics>(graphics), MakeConfig(*graphics),
      capabilities),
    [](Renderer* renderer) {
      if (renderer != nullptr) {
        renderer->OnShutdown();
        delete renderer;
      }
    });
}

NOLINT_TEST(DiagnosticsTypesTest, FeatureFlagsUseStableStringOrder)
{
  const auto features = DiagnosticsFeature::kGpuTimeline
    | DiagnosticsFeature::kFrameLedger
    | DiagnosticsFeature::kCaptureManifest;

  EXPECT_EQ(oxygen::vortex::to_string(features),
    "FrameLedger | GpuTimeline | CaptureManifest");
  EXPECT_EQ(
    oxygen::vortex::to_string(DiagnosticsFeature::kNone), "None");
}

NOLINT_TEST(DiagnosticsTypesTest, EnumStringsCoverPublicDiagnosticsEnums)
{
  EXPECT_EQ(
    oxygen::vortex::to_string(DiagnosticsSeverity::kWarning), "Warning");
  EXPECT_EQ(
    oxygen::vortex::to_string(DiagnosticsPassKind::kCompute), "Compute");
  EXPECT_EQ(oxygen::vortex::to_string(
              DiagnosticsDebugPath::kForwardMeshVariant),
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
    DiagnosticsConfig { .default_features = DiagnosticsFeature::kFrameLedger },
  };

  service.SetShaderDebugMode(ShaderDebugMode::kDirectionalShadowMask);
  service.BeginFrame(SequenceNumber { 42U });
  service.RecordPass(DiagnosticsPassRecord {
    .name = "Stage8.ShadowDepth.Directional",
    .kind = DiagnosticsPassKind::kGraphics,
    .executed = true,
    .outputs = { "Vortex.DirectionalShadowSurface" },
  });
  service.RecordProduct(DiagnosticsProductRecord {
    .name = "Vortex.DirectionalShadowSurface",
    .producer_pass = "Stage8.ShadowDepth.Directional",
    .published = true,
    .valid = true,
  });
  service.ReportIssue(DiagnosticsIssue {
    .severity = DiagnosticsSeverity::kWarning,
    .code = "debug-mode.missing-product",
    .message = "test issue",
  });
  service.EndFrame();

  const auto snapshot = service.GetLatestSnapshot();
  EXPECT_EQ(snapshot.frame_index, SequenceNumber { 42U });
  EXPECT_EQ(snapshot.active_shader_debug_mode,
    ShaderDebugMode::kDirectionalShadowMask);
  ASSERT_EQ(snapshot.passes.size(), 1U);
  EXPECT_EQ(snapshot.passes[0].name, "Stage8.ShadowDepth.Directional");
  ASSERT_EQ(snapshot.products.size(), 1U);
  EXPECT_EQ(snapshot.products[0].name, "Vortex.DirectionalShadowSurface");
  ASSERT_EQ(snapshot.issues.size(), 1U);
  EXPECT_EQ(snapshot.issues[0].code, "debug-mode.missing-product");
}

NOLINT_TEST(DiagnosticsServiceTest, DisabledLedgerDoesNotAccumulateRecords)
{
  auto service = DiagnosticsService {
    kDiagnosticsCapability,
    DiagnosticsConfig { .default_features = DiagnosticsFeature::kNone },
  };

  service.BeginFrame(SequenceNumber { 7U });
  service.RecordPass(DiagnosticsPassRecord { .name = "Skipped" });
  service.RecordProduct(DiagnosticsProductRecord { .name = "Skipped" });
  service.ReportIssue(DiagnosticsIssue { .code = "diag.feature-unavailable" });
  service.EndFrame();

  const auto snapshot = service.GetLatestSnapshot();
  EXPECT_EQ(snapshot.frame_index, SequenceNumber { 7U });
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

  EXPECT_EQ(
    renderer->GetShaderDebugMode(), ShaderDebugMode::kSceneDepthLinear);
  EXPECT_EQ(renderer->GetDiagnosticsService().GetShaderDebugMode(),
    ShaderDebugMode::kSceneDepthLinear);
}

NOLINT_TEST(DiagnosticsServiceTest, ExposesShaderDebugModeRegistry)
{
  auto service = DiagnosticsService {
    kDiagnosticsCapability,
    DiagnosticsConfig { .default_features
      = DiagnosticsFeature::kShaderDebugModes },
  };

  EXPECT_FALSE(service.EnumerateShaderDebugModes().empty());
  const auto resolved = service.FindShaderDebugMode("directional-shadow-mask");
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, ShaderDebugMode::kDirectionalShadowMask);
  EXPECT_FALSE(service.FindShaderDebugMode("missing-mode").has_value());
}

NOLINT_TEST(DiagnosticsServiceTest,
  GpuTimelineFacadeForwardsAndConvertsProfilerDiagnostics)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  auto profiler = GpuTimelineProfiler(
    oxygen::observer_ptr<Graphics> { graphics.get() });
  auto service = DiagnosticsService {
    kDiagnosticsCapability,
    DiagnosticsConfig { .default_features = DiagnosticsFeature::kFrameLedger
        | DiagnosticsFeature::kGpuTimeline },
  };

  service.SetGpuTimelineProfiler(oxygen::observer_ptr { &profiler });
  service.SetGpuTimelineRetainLatestFrame(true);
  service.SetGpuTimelineMaxScopesPerFrame(1U);
  service.SetGpuTimelineEnabled(true);

  EXPECT_TRUE(service.IsGpuTimelineEnabled());

  profiler.OnFrameStart(SequenceNumber { 1U });
  auto recorder = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics), "DiagnosticsTimeline", true);
  recorder->SetTelemetryCollector(
    oxygen::observer_ptr<oxygen::graphics::IGpuProfileCollector> { &profiler });

  {
    oxygen::graphics::GpuEventScope first(*recorder, "First",
      oxygen::profiling::ProfileGranularity::kTelemetry);
  }
  {
    oxygen::graphics::GpuEventScope second(*recorder, "Second",
      oxygen::profiling::ProfileGranularity::kTelemetry);
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(SequenceNumber { 2U });

  ASSERT_TRUE(service.GetLatestGpuTimelineFrame().has_value());

  service.BeginFrame(SequenceNumber { 2U });
  service.SyncGpuTimelineDiagnostics();
  service.EndFrame();

  const auto snapshot = service.GetLatestSnapshot();
  EXPECT_TRUE(snapshot.gpu_timeline_enabled);
  EXPECT_TRUE(snapshot.gpu_timeline_frame_available);
  auto found_overflow = false;
  for (const auto& issue : snapshot.issues) {
    found_overflow = found_overflow || issue.code == "gpu-timeline.overflow";
  }
  EXPECT_TRUE(found_overflow);
}

} // namespace
