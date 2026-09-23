//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Profiling/CpuScopeObserver.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Support/CpuTimingCapture.h>
#include <Oxygen/Vortex/Test/Support/D3D12MemoryCapture.h>

namespace oxygen::vortex::testing {
namespace {
  using Clock = std::chrono::steady_clock;
  constexpr std::uint32_t kWidth = 1920U;
  constexpr std::uint32_t kHeight = 1080U;
  constexpr std::size_t kLightCount = 33U;
  constexpr std::size_t kMaximumSamples = 65536U;
  constexpr std::size_t kCpuRecordsPerFrame = 16U;
  constexpr std::uint32_t kWarmupViewId = 9000U;
  constexpr std::uint32_t kMeasuredViewId = 100U;
  constexpr std::uint32_t kGpuScopeCapacity = 128U;

  struct FrameSample {
    unsigned sequence { 0U };
    double wall_ms { 0.0 };
    double begin_frame_ms { 0.0 };
    double submit_ms { 0.0 };
  };

  auto Milliseconds(const Clock::duration duration) -> double
  {
    return std::chrono::duration<double, std::milli>(duration).count();
  }

  class LightingCollectionBenchmark : public exposure::ExposureLightingGpuTest {
  protected:
    auto BackendConfigJson() const -> std::string override
    {
      return R"({"enable_debug_layer":false,"enable_vsync":false})";
    }
    auto AdditionalCapabilities() const -> CapabilitySet override
    {
      return RendererCapabilityFamily::kDiagnosticsAndProfiling;
    }
    auto SetUp() -> void override
    {
      initial_scene_capacity = kLightCount + 2U;
      ExposureLightingGpuTest::SetUp();
      FailureBackend().SetRecorderNameCollectionEnabled(false);
    }
    auto RunCollection(bool smoke, std::span<const ShadingMode> paths) -> void;
    auto RenderFrame(bool forward, CpuTimingCapture* cpu,
      D3D12MemoryCapture* memory, MemorySampleId sample) -> FrameSample;
  };

  auto LightingCollectionBenchmark::RenderFrame(const bool forward,
    CpuTimingCapture* cpu, D3D12MemoryCapture* memory,
    const MemorySampleId sample) -> FrameSample
  {
    const auto started = Clock::now();
    const auto slot = frame::Slot { sequence % frame::kFramesInFlight.get() };
    const auto current = frame::SequenceNumber { ++sequence };
    auto observer = std::optional<profiling::ScopedCpuScopeObserver> {};
    if (cpu != nullptr) {
      cpu->BeginFrame(current);
      observer.emplace(*cpu);
    }
    Backend().BeginFrame(current, slot);
    const auto began = Clock::now();
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(
      current, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    auto input
      = CompositionView::ForScene(ViewId { surface_view_id }, view, camera);
    input.view_state_handle
      = CompositionView::ViewStateHandle { surface_view_id };
    input.render_settings.exposure = settings;
    CHECK_F(renderer_->PublishRuntimeCompositionView(frame,
              {
                .composition_view = input,
                .render_target = observer_ptr { framebuffer.get() },
                .composite_source = observer_ptr { framebuffer.get() },
              },
              forward ? ShadingMode::kForward : ShadingMode::kDeferred)
      != kInvalidViewId);
    auto loop = co::testing::TestEventLoop {};
    // Run finishes synchronously before the closure or captures leave scope.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    co::Run(loop, [&] -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr { &frame });
      co_await renderer_->OnRender(observer_ptr { &frame });
      co_await renderer_->OnCompositing(observer_ptr { &frame });
    });
    CHECK_F(probe->draws == 1U && probe->color != nullptr);
    renderer_->OnFrameEnd(observer_ptr { &frame });
    Backend().EndFrame(current, slot);
    if (memory != nullptr) {
      CHECK_F(memory->Record(sample, current, Backend().GetMemoryStatistics()));
    }
    const auto ended = Clock::now();
    return {
      .sequence = sequence,
      .wall_ms = Milliseconds(ended - started),
      .begin_frame_ms = Milliseconds(began - started),
      .submit_ms = Milliseconds(ended - began),
    };
  }

  auto LightingCollectionBenchmark::RunCollection(
    const bool smoke, const std::span<const ShadingMode> paths) -> void
  {
#ifndef NDEBUG
    (void)smoke;
    (void)paths;
    GTEST_SKIP() << "Collection overhead requires Release.";
#else
    // Frozen authored recipe, matching the mixed-light image qualification.
    constexpr float kPositionStepX = 0.25F;
    constexpr float kPositionStepY = 0.2F;
    constexpr float kFluxStep = 0.1F;
    constexpr float kInnerHalfAngle = 0.2F;
    constexpr float kOuterHalfAngle = 1.2F;
    constexpr float kHalfTurnQuaternion = 0.70710678F;
    constexpr std::size_t kColumns = 7U;
    constexpr float kCenterColumn = 3.0F;
    constexpr float kMinimumRangeM = 3.0F;
    constexpr float kRangeStep = 0.25F;
    constexpr auto kLightTints = std::array {
      glm::vec3 { 0.2F, 0.5F, 1.0F },
      glm::vec3 { 1.0F, 0.25F, 0.1F },
      glm::vec3 { 0.1F, 1.0F, 0.4F },
    };
    for (std::size_t index = 0U; index < kLightCount; ++index) {
      auto node = scene->CreateNode(
        "Collection fixture light " + std::to_string(index));
      const auto row = index / kColumns;
      node.GetTransform().SetLocalPosition({
        (static_cast<float>(index % kColumns) - kCenterColumn) * kPositionStepX,
        (static_cast<float>(row) - 2.0F) * kPositionStepY,
        0.0F,
      });
      const auto initialize = [&](auto& light) -> void {
        light.Common().casts_shadows = false;
        light.Common().color_rgb = kLightTints.at(index % kLightTints.size());
        light.Common().exposure_compensation_ev
          = static_cast<float>(index % kLightTints.size()) - 1.0F;
        light.SetRange(kMinimumRangeM
          + (static_cast<float>(index % kLightTints.size()) * kRangeStep));
        light.SetLuminousFluxLm(1.0F + (static_cast<float>(index) * kFluxStep));
      };
      if (index % 2U == 0U) {
        auto light = std::make_unique<scene::PointLight>();
        initialize(*light);
        ASSERT_TRUE(node.AttachLight(std::move(light)));
      } else {
        auto light = std::make_unique<scene::SpotLight>();
        initialize(*light);
        light->SetInnerConeAngleRadians(kInnerHalfAngle);
        light->SetOuterConeAngleRadians(kOuterHalfAngle);
        ASSERT_TRUE(node.AttachLight(std::move(light)));
        node.GetTransform().SetLocalRotation(
          { kHalfTurnQuaternion, kHalfTurnQuaternion, 0.0F, 0.0F });
      }
    }
    view.viewport.width = static_cast<float>(kWidth);
    view.viewport.height = static_cast<float>(kHeight);
    auto lens = camera.GetCameraAs<scene::PerspectiveCamera>();
    ASSERT_TRUE(lens.has_value());
    lens->get().SetViewport(view.viewport);
    lens->get().SetAspectRatio(static_cast<float>(kWidth) / kHeight);
    auto output = CreateRegisteredTexture({
      .width = kWidth,
      .height = kHeight,
      .format = Format::kRGBA16Float,
      .is_render_target = true,
      .initial_state = graphics::ResourceStates::kCommon,
    });
    framebuffer = Backend().CreateFramebuffer(
      graphics::FramebufferDesc {}.AddColorAttachment(output));
    SetSurface(data::MaterialDomain::kOpaque);
    renderer_->GetDiagnosticsService().SetHdrPrecisionControl(
      HdrPrecisionControl::kProduction);
    surface_view_id = kWarmupViewId;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 3U));
    surface_view_id = kMeasuredViewId;
    frame.SetScene(observer_ptr { scene.get() });
    auto timing = frame.GetModuleTimingData();
    constexpr auto kFrameNanoseconds = 16'666'667;
    timing.game_delta_time = time::CanonicalDuration {
      std::chrono::nanoseconds { kFrameNanoseconds }
    };
    frame.SetModuleTimingData(
      timing, engine::internal::EngineTagFactory::Get());
    auto& diagnostics = renderer_->GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
    diagnostics.SetGpuTimelineMaxScopesPerFrame(kGpuScopeCapacity);
    diagnostics.SetGpuTimelineRetainLatestFrame(false);
    diagnostics.SetGpuTimelineEnabled(true);
    ASSERT_TRUE(diagnostics.IsGpuTimelineEnabled());
    const auto directory = std::filesystem::path { OXYGEN_EXPOSURE_WORKSPACE }
      / "out/build-ninja/analysis/vortex/exposure-lightbench/ex07b"
      / ("collection-"
        + std::to_string(Clock::now().time_since_epoch().count()));
    ASSERT_TRUE(std::filesystem::create_directory(directory));
    RecordProperty("report_directory", directory.string());
    auto results = nlohmann::json::array();
    const auto capacity = smoke ? 8U : kMaximumSamples;
    const auto minimum_samples = smoke ? 8U : 3600U;
    const auto sample_seconds = std::chrono::seconds { smoke ? 0 : 30 };
    const auto warm_seconds = std::chrono::seconds { smoke ? 0 : 3 };
    const auto minimum_warm_frames = smoke ? 3U : 300U;
    const auto prior_verbosity = loguru::g_global_verbosity;
    loguru::g_global_verbosity = loguru::Verbosity_WARNING;
    const auto restore = ScopeGuard([&] noexcept -> void {
      FailureBackend().count_resource_creations = false;
      loguru::g_global_verbosity = prior_verbosity;
    });
    for (const auto path : paths) {
      const bool forward = path == ShadingMode::kForward;
      unsigned window = 0U;
      auto reference_image = std::vector<exposure::Pixel> {};
      auto reference_format = std::optional<Format> {};
      for (const bool enabled : { false, true, true, false }) {
        FailureBackend().count_resource_creations = false;
        const auto warm_start = Clock::now();
        unsigned warmed = 0U;
        while (warmed < minimum_warm_frames
          || Clock::now() - warm_start < warm_seconds) {
          (void)RenderFrame(forward, nullptr, nullptr, MemorySampleId { 0U });
          ++warmed;
        }
        const auto expected_format = probe->color->GetDescriptor().format;
        if (reference_format) {
          ASSERT_EQ(expected_format, *reference_format);
        } else {
          reference_format = expected_format;
        }
        const auto* lighting
          = RendererPublicationProbe::GetLightingService(*renderer_);
        ASSERT_NE(lighting, nullptr);
        ASSERT_EQ(
          lighting->GetLastGridBuildState().local_light_count, kLightCount);
        auto samples = std::vector<FrameSample> {};
        samples.reserve(capacity);
        auto cpu = CpuTimingCapture({
          .record_capacity
          = CpuTimingRecordCapacity { capacity * kCpuRecordsPerFrame },
        });
        auto memory = D3D12MemoryCapture(MemorySampleCapacity { capacity });
        const auto before = FailureBackend().resource_creations.Snapshot();
        ASSERT_TRUE(before.has_value());
        FailureBackend().count_resource_creations = enabled;
        const auto started = Clock::now();
        auto previous_end = started;
        while (samples.size() < minimum_samples
          || Clock::now() - started < sample_seconds) {
          ASSERT_LT(samples.size(), capacity)
            << "Declared capture capacity exhausted";
          auto sample = RenderFrame(forward, enabled ? &cpu : nullptr,
            enabled ? &memory : nullptr, MemorySampleId { samples.size() });
          const auto ended = Clock::now();
          sample.wall_ms = Milliseconds(ended - previous_end);
          previous_end = ended;
          samples.push_back(sample);
          CHECK_F(probe->color->GetDescriptor().format == expected_format);
        }
        const auto elapsed = Milliseconds(Clock::now() - started);
        FailureBackend().count_resource_creations = false;
        const auto after = FailureBackend().resource_creations.Snapshot();
        ASSERT_TRUE(after.has_value());
        // Complete pending work only after the declared measurement window.
        WaitForQueueIdle();
        const auto image = ReadFloatTexture(*probe->color, true);
        if (reference_image.empty()) {
          reference_image = image;
        } else {
          ASSERT_EQ(image, reference_image)
            << "Collection changed the rendered image";
        }
        const auto stem = std::string(forward ? "forward-" : "deferred-")
          + (enabled ? "on-" : "off-") + std::to_string(window++);
        auto csv = std::ofstream(directory / (stem + ".csv"));
        csv.precision(std::numeric_limits<double>::max_digits10);
        csv << "frame_seq,wall_ms,frame_start_ms,submission_ms\n";
        for (const auto& sample : samples) {
          csv << sample.sequence << ',' << sample.wall_ms << ','
              << sample.begin_frame_ms << ',' << sample.submit_ms << '\n';
        }
        csv.close();
        ASSERT_TRUE(csv.good());
        auto row = nlohmann::json {
          { "window", stem },
          { "collection_enabled", enabled },
          { "samples", samples.size() },
          { "elapsed_ms", elapsed },
          { "color_format", static_cast<unsigned>(expected_format) },
          { "buffer_creations", after->buffers - before->buffers },
          { "texture_creations", after->textures - before->textures },
          {
            "requested_buffer_bytes",
            after->requested_buffer_bytes.get()
              - before->requested_buffer_bytes.get(),
          },
        };
        if (enabled) {
          row.update({ { "cpu", cpu.Save(directory / (stem + ".cpu.csv")) } });
          auto memory_file = std::ofstream(directory / (stem + ".memory.json"));
          memory_file << memory.Report();
          memory_file.close();
          ASSERT_TRUE(memory_file.good());
        }
        results.push_back(std::move(row));
        std::cout << "Completed collection window " << stem << ": "
                  << samples.size() << " frames\n"
                  << std::flush;
      }
    }
    const auto adapter = Backend().GetCurrentDevice()->GetAdapterLuid();
    auto manifest = std::ofstream(directory / "manifest.json");
    manifest << nlohmann::json {
      { "schema", 1U }, { "smoke", smoke }, { "complete", true },
      { "width", kWidth }, { "height", kHeight }, { "lights", kLightCount },
      { "adapter_luid_low", adapter.LowPart }, { "adapter_luid_high", adapter.HighPart },
      { "control", "GPU timeline queries only; no CPU observer or memory sampling" },
      { "candidate", "same GPU queries plus compact CPU scopes, one allocator sample per frame and successful factory-call counters" },
      { "scope", "incremental collection overhead; not physical renderer or presentation-budget qualification" },
      { "windows", std::move(results) },
    }.dump(2);
    manifest.close();
    ASSERT_TRUE(manifest.good());
#endif
  }

  NOLINT_TEST_F(LightingCollectionBenchmark, DISABLED_CollectionSmoke)
  {
    constexpr auto kPaths
      = std::array { ShadingMode::kDeferred, ShadingMode::kForward };
    RunCollection(true, kPaths);
  }
  NOLINT_TEST_F(
    LightingCollectionBenchmark, DISABLED_ReleaseDeferredCollectionOnOff)
  {
    constexpr auto kPaths = std::array { ShadingMode::kDeferred };
    RunCollection(false, kPaths);
  }
  NOLINT_TEST_F(
    LightingCollectionBenchmark, DISABLED_ReleaseForwardCollectionOnOff)
  {
    constexpr auto kPaths = std::array { ShadingMode::kForward };
    RunCollection(false, kPaths);
  }
} // namespace
} // namespace oxygen::vortex::testing
