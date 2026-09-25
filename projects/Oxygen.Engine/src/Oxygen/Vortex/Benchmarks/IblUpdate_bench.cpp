//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/CapturedSkySource.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  class IblUpdateBenchmark : public ExposureGpuTest {
  protected:
    auto SetUp() -> void override
    {
      ExposureGpuTest::SetUp();
      pass_.reset();
      renderer_->OnShutdown();
      auto config = RendererConfig {};
      config.upload_queue_key = QueueKeyFor().get();
      renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
        kPhase1DefaultRuntimeCapabilityFamilies
          | RendererCapabilityFamily::kEnvironmentLighting
          | RendererCapabilityFamily::kDiagnosticsAndProfiling);
    }

    auto BackendConfigJson() const -> std::string override
    {
      auto config = nlohmann::json::parse(ExposureGpuTest::BackendConfigJson());
      config["enable_debug_layer"] = false;
      return config.dump();
    }
  };

  NOLINT_TEST_F(IblUpdateBenchmark, DISABLED_CapturedSunUpdates)
  {
#ifndef NDEBUG
    GTEST_SKIP() << "Native timing requires Release";
#endif
    char* output = nullptr;
    std::size_t length = 0;
    ASSERT_EQ(_dupenv_s(&output, &length, "OXYGEN_IBL_TIMING_OUTPUT"), 0);
    const auto owned
      = std::unique_ptr<char, decltype(&std::free)>(output, &std::free);
    ASSERT_NE(output, nullptr);
    const auto directory = std::filesystem::path(output);
    ASSERT_FALSE(std::filesystem::exists(directory));
    std::filesystem::create_directories(directory);
    auto& diagnostics = renderer_->GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
    diagnostics.SetGpuTimelineMaxScopesPerFrame(128U);
    diagnostics.SetGpuTimelineRetainLatestFrame(false);
    diagnostics.SetGpuTimelineEnabled(true);
    const auto verbosity = loguru::g_global_verbosity;
    loguru::g_global_verbosity = loguru::Verbosity_WARNING;
    const auto restore
      = ScopeGuard([&] noexcept { loguru::g_global_verbosity = verbosity; });
    namespace env = environment::internal;
    auto source = env::CapturedSkySource(*renderer_);
    auto processor = env::IblGpuProcessor(*renderer_);
    auto brdf_owner = env::IblBrdfResources(Backend());
    using Clock = std::chrono::steady_clock;
    const auto first_begin = Clock::now();
    const auto brdf = brdf_owner.Prepare();
    ASSERT_TRUE(brdf.has_value());
    auto state = env::StableAtmosphereState {};
    state.atmosphere_revision = 1U;
    state.view_products.atmosphere.enabled = true;
    state.view_products.atmosphere_light_count = 1U;
    auto& light = state.view_products.atmosphere_lights[0];
    light.enabled = true;
    light.illuminance_rgb_lux = glm::vec3(120000.0F);
    light.disk_luminance_scale_rgb = glm::vec3(1.0F);
    GpuFogParams fog {};
    fog.flags = kGpuFogFlagEnabled | kGpuFogFlagHeightFogEnabled
      | kGpuFogFlagVisibleInRealTimeSkyCaptures;
    fog.primary_density = 0.01F;
    fog.primary_height_falloff = 0.001F;
    fog.fog_inscattering_luminance_rgb = { 0.2F, 0.3F, 0.4F };
    auto frame = engine::FrameContext {};
    constexpr unsigned warmup = 120U;
    constexpr unsigned samples = 1800U;
    std::shared_ptr<const env::IblGpuProducts> published;
    double first_use_wall_ms = 0;
    auto cpu = std::ofstream(directory / "updates.csv");
    cpu << "frame_seq,cpu_record_ms,storage_creations,allocated_slots\n";
    const auto paced_begin = Clock::now();
    for (unsigned index = 0; index < warmup + samples + 4U; ++index) {
      const auto sequence = frame::SequenceNumber { index + 1U };
      const auto slot = frame::Slot { index % frame::kFramesInFlight.get() };
      Backend().BeginFrame(sequence, slot);
      frame.SetFrameSequenceNumber(
        sequence, engine::internal::EngineTagFactory::Get());
      frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
      renderer_->OnFrameStart(observer_ptr { &frame });
      if (index == warmup) {
        ASSERT_TRUE(diagnostics.RequestGpuTimelineRecording(
          directory / "gpu.json", samples));
      }
      ctx_.frame_sequence = sequence;
      ctx_.frame_slot = slot;
      ctx_.current_view.view_id = kInvalidViewId;
      const float angle = 0.2F + float(index) * 0.0003F;
      light.direction_to_light_ws
        = glm::normalize(glm::vec3(std::cos(angle), 0.0F, std::sin(angle)));
      state.light_revision = index + 1U;
      const auto begin = Clock::now();
      auto next
        = source.Process(ctx_, state, fog, processor, *brdf, {}, index + 1U);
      ASSERT_TRUE(next.has_value()) << static_cast<int>(next.error());
      published = std::move(*next);
      const auto elapsed
        = std::chrono::duration<double, std::milli>(Clock::now() - begin)
            .count();
      auto loop = co::testing::TestEventLoop {};
      // Run completes before the closure leaves scope. The normal compositing
      // tail resolves GPU timestamps even when no surface is being rendered.
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
      co::Run(
        loop, [&]() -> co::Co<void> { co_await renderer_->OnCompositing({}); });
      renderer_->OnFrameEnd(observer_ptr { &frame });
      Backend().EndFrame(sequence, slot);
      if (index == 0U) {
        WaitForQueueIdle();
        first_use_wall_ms = std::chrono::duration<double, std::milli>(
          Clock::now() - first_begin)
                              .count();
      }
      if (index >= warmup && index < warmup + samples) {
        const auto stats = processor.GetStats();
        cpu << index + 1U << ',' << elapsed << ',' << stats.storage_creations
            << ',' << stats.allocated << '\n';
      }
      std::this_thread::sleep_until(paced_begin
        + std::chrono::duration_cast<Clock::duration>(
          std::chrono::duration<double>(double(index + 1U) / 60.0)));
    }
    WaitForQueueIdle();
    auto input = std::ifstream(directory / "gpu.json");
    ASSERT_TRUE(input.good());
    const auto report = nlohmann::json::parse(input);
    ASSERT_EQ(report.at("complete"), true);
    ASSERT_EQ(report.at("timing_valid"), true);
    ASSERT_EQ(report.at("frames").size(), samples);
    for (const auto& sample : report.at("frames")) {
      unsigned process_scopes = 0;
      for (const auto& scope : sample.at("scopes")) {
        ASSERT_EQ(scope.at("valid"), true);
        if (scope.at("name") == "Vortex.Environment.IBL.Process")
          ++process_scopes;
      }
      EXPECT_EQ(process_scopes, 1U);
    }
    auto manifest = std::ofstream(directory / "run.json");
    manifest << nlohmann::json { { "samples", samples }, { "warmup", warmup },
      { "hz", 60 }, { "first_use_wall_ms", first_use_wall_ms },
      { "scope",
        "Isolated current immediate atmosphere+fog capture/convolution; "
        "full-scene latency, incremental scheduling and acceptance remain "
        "open" } }
                  .dump(2)
             << '\n';
  }
} // namespace
} // namespace oxygen::vortex::testing::exposure
