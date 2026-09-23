//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Profiling/CpuScopeObserver.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Support/CpuTimingCapture.h>

namespace oxygen::vortex::testing {
namespace {
  class LightingInstrumentationGpuTest
    : public exposure::ExposureLightingGpuTest {
  protected:
    auto AdditionalCapabilities() const -> CapabilitySet override
    {
      return RendererCapabilityFamily::kDiagnosticsAndProfiling
        | RendererCapabilityFamily::kShadowing;
    }

    auto SetUp() -> void override
    {
      ExposureLightingGpuTest::SetUp();
      const auto suffix
        = std::chrono::steady_clock::now().time_since_epoch().count();
      directory_ = std::filesystem::temp_directory_path()
        / ("oxygen-lighting-instruments-" + std::to_string(suffix));
      owns_directory_ = std::filesystem::create_directory(directory_);
      ASSERT_TRUE(owns_directory_);
      auto light = std::make_unique<scene::PointLight>();
      light->Common().casts_shadows = false;
      light->SetLuminousFluxLm(1.0F);
      light->SetRange(4.0F);
      auto node = scene->CreateNode("Instrument qualification point");
      ASSERT_TRUE(node.AttachLight(std::move(light)));
      SetSurface(data::MaterialDomain::kOpaque);
    }

    auto TearDown() -> void override
    {
      auto ignored = std::error_code {};
      if (owns_directory_) {
        for (const auto* name : { "forward.csv", "deferred.csv" }) {
          std::filesystem::remove(directory_ / name, ignored);
        }
        std::filesystem::remove(directory_, ignored);
      }
      ExposureLightingGpuTest::TearDown();
    }

    std::filesystem::path directory_;
    bool owns_directory_ { false };
  };

  NOLINT_TEST_F(
    LightingInstrumentationGpuTest, NativePhasesHaveBoundedCpuAndGpuCoverage)
  {
    // Load geometry/materials before the published-view measurement lifecycle.
    surface_view_id = 9000U;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 3U));
    surface_view_id = 100U;
    auto& diagnostics = renderer_->GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
    diagnostics.SetGpuTimelineMaxScopesPerFrame(128U);
    diagnostics.SetGpuTimelineRetainLatestFrame(true);
    diagnostics.SetGpuTimelineEnabled(true);
    ASSERT_TRUE(diagnostics.IsGpuTimelineEnabled());

    for (const bool forward : { false, true }) {
      SCOPED_TRACE(forward);
      const auto render = [&] -> void {
        ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
        // This helper intentionally omits final composition. Resolve after
        // its scene commands have submitted, before the next frame starts.
        RendererPublicationProbe::GetGpuTimelineProfiler(*renderer_)
          .OnFrameRecordTailResolve();
        WaitForQueueIdle();
      };
      for (unsigned warmup = 0U; warmup < 3U; ++warmup) {
        ASSERT_NO_FATAL_FAILURE(render());
      }
      auto cpu = CpuTimingCapture({
        .record_capacity = CpuTimingRecordCapacity { 256U },
        .detailed = true,
      });
      const auto measured_sequence = frame::SequenceNumber { sequence + 1U };
      cpu.BeginFrame(measured_sequence);
      {
        const auto observer = profiling::ScopedCpuScopeObserver(cpu);
        ASSERT_NO_FATAL_FAILURE(render());
      }
      const auto path = directory_ / (forward ? "forward.csv" : "deferred.csv");
      const auto cpu_report = cpu.Save(path);
      EXPECT_EQ(cpu_report.at("complete"), true);
      EXPECT_LE(cpu_report.at("records").get<unsigned>(), 256U);
      auto input = std::ifstream(path);
      ASSERT_TRUE(input.good());
      auto text = std::ostringstream {};
      text << input.rdbuf();
      const auto csv = text.str();
      for (const auto* phase : {
             "Vortex.Lighting.GatherSelection",
             "Vortex.Lighting.BuildGrid",
             "Vortex.Lighting.ResolveEvaluation",
             "Vortex.Lighting.PublishFrame",
             "Vortex.Shadows.RecordDepths",
             "Vortex.Lighting.PublishShadowReferences",
           }) {
        EXPECT_TRUE(csv.contains(phase)) << phase;
      }
      for (const auto* phase : {
             "Vortex.Lighting.BuildDeferredPackets",
             "Vortex.Lighting.RecordDeferred",
           }) {
        EXPECT_EQ(csv.contains(phase), !forward) << phase;
      }

      // Advance once to publish the preceding, already completed query range.
      // These native correctness checks deliberately wait; they are not timings
      // admitted to a performance baseline or overhead comparison.
      ASSERT_NO_FATAL_FAILURE(render());
      const auto gpu
        = RendererPublicationProbe::GetGpuTimelineProfiler(*renderer_)
            .GetLastPublishedFrame();
      ASSERT_TRUE(gpu.has_value());
      EXPECT_EQ(gpu->frame_sequence, measured_sequence.get());
      EXPECT_TRUE(gpu->profiling_enabled);
      EXPECT_FALSE(gpu->overflowed);
      EXPECT_TRUE(gpu->diagnostics.empty());
      EXPECT_LE(gpu->used_query_slots, 256U);
      std::uint64_t frequency = 0U;
      ASSERT_TRUE(Backend()
          .GetCommandQueue(QueueKeyFor())
          ->TryGetTimestampFrequency(frequency));
      EXPECT_EQ(gpu->timestamp_frequency_hz, frequency);
      ASSERT_GT(frequency, 0U);
      ASSERT_FALSE(gpu->scopes.empty());
      for (const auto& scope : gpu->scopes) {
        EXPECT_TRUE(scope.valid) << scope.display_name;
        EXPECT_TRUE(std::isfinite(scope.duration_ms));
        EXPECT_GE(scope.duration_ms, 0.0F);
        EXPECT_GE(scope.end_ms, scope.start_ms);
        EXPECT_LT(scope.begin_query_slot, gpu->used_query_slots);
        EXPECT_LT(scope.end_query_slot, gpu->used_query_slots);
        if (scope.depth != 0U) {
          ASSERT_LT(scope.parent_scope_id, gpu->scopes.size());
          const auto& parent = gpu->scopes.at(scope.parent_scope_id);
          EXPECT_GE(scope.start_ms, parent.start_ms);
          EXPECT_LE(scope.end_ms, parent.end_ms);
        }
      }
      const auto deferred
        = std::ranges::find_if(gpu->scopes, [](const auto& scope) -> bool {
            return scope.display_name == "Vortex.Stage12.DeferredLighting";
          });
      EXPECT_EQ(deferred != gpu->scopes.end(), !forward);
      const auto forward_base
        = std::ranges::find_if(gpu->scopes, [](const auto& scope) -> bool {
            return scope.display_name == "Vortex.Stage9.BasePass.Forward";
          });
      EXPECT_EQ(forward_base != gpu->scopes.end(), forward);
      RecordProperty(forward ? "forward_cpu_records" : "deferred_cpu_records",
        cpu_report.at("records").get<unsigned>());
      RecordProperty(forward ? "forward_gpu_scopes" : "deferred_gpu_scopes",
        gpu->scopes.size());
    }
  }
} // namespace
} // namespace oxygen::vortex::testing
