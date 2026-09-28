//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <ratio>
#include <stdlib.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/ExposureBenchmarkScene.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  class IblSceneBenchmark : public ExposureLightingGpuTest {
  protected:
    enum class Mode { kStatic, kRuntime, kAuthoring };
    auto RunScene(Mode mode) -> void;
    auto SetUp() -> void override
    {
      initial_scene_capacity = 64U;
      ExposureLightingGpuTest::SetUp();
    }
    auto AdditionalCapabilities() const -> CapabilitySet override
    {
      return RendererCapabilityFamily::kDiagnosticsAndProfiling
        | RendererCapabilityFamily::kShadowing;
    }
    auto BackendConfigJson() const -> std::string override
    {
      return R"({"enable_debug_layer":false,"enable_vsync":false})";
    }
  };

  NOLINT_TEST_F(IblSceneBenchmark, DISABLED_Static) { RunScene(Mode::kStatic); }
  NOLINT_TEST_F(IblSceneBenchmark, DISABLED_Runtime)
  {
    RunScene(Mode::kRuntime);
  }
  NOLINT_TEST_F(IblSceneBenchmark, DISABLED_Authoring)
  {
    RunScene(Mode::kAuthoring);
  }

  auto IblSceneBenchmark::RunScene(const Mode mode) -> void
  {
#ifdef NDEBUG
    constexpr bool kBenchmarkEnabled = true;
#else
    constexpr bool kBenchmarkEnabled = false;
#endif
    if constexpr (!kBenchmarkEnabled) {
      GTEST_SKIP() << "Native scene timing requires Release";
    } else {
      constexpr unsigned width = 1920U;
      constexpr unsigned height = 1080U;
      constexpr unsigned warmup = 120U;
      constexpr unsigned samples = 1800U;
      using Clock = std::chrono::steady_clock;
      char* output = nullptr;
      std::size_t length {};
      ASSERT_EQ(_dupenv_s(&output, &length, "OXYGEN_IBL_TIMING_OUTPUT"), 0);
      const auto owned
        = std::unique_ptr<char, decltype(&std::free)>(output, &std::free);
      ASSERT_NE(output, nullptr);
      const auto directory = std::filesystem::path(output);
      ASSERT_FALSE(std::filesystem::exists(directory));
      std::filesystem::create_directories(directory);
      FailureBackend().track_resources = true;
      FailureBackend().count_resource_creations = true;
      FailureBackend().SetRecorderNameCollectionEnabled(false);
      const auto verbosity = loguru::g_global_verbosity;
      loguru::g_global_verbosity = loguru::Verbosity_WARNING;
      const ScopeGuard restore([&]() noexcept {
        probe->inspect = {};
        FailureBackend().track_resources = false;
        FailureBackend().count_resource_creations = false;
        loguru::g_global_verbosity = verbosity;
      });
      ASSERT_TRUE(scene->DestroyNode(mesh_node));
      ASSERT_TRUE(scene->DestroyNode(camera));
      const auto recipe = PopulateMixedExposureBenchmarkScene(*scene);
      camera = recipe.main_camera;
      view.viewport.width = float(width);
      view.viewport.height = float(height);
      auto lens = camera.GetCameraAs<scene::PerspectiveCamera>();
      if (!lens) {
        FAIL() << "Expected benchmark camera";
      }
      lens->get().SetViewport(view.viewport);
      lens->get().SetAspectRatio(float(width) / float(height));
      auto second
        = scene->CreateNode("IBL benchmark secondary atmosphere light");
      auto light = std::make_unique<scene::DirectionalLight>();
      light->SetIntensityLux(1000.0F);
      light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kSecondary);
      ASSERT_TRUE(second.AttachLight(std::move(light)));
      auto& sky = *scene->GetEnvironment()
                     ->TryGetSystem<scene::environment::SkyLight>();
      sky.SetSource(scene::environment::SkyLightSource::kCapturedScene);
      sky.SetSpecularIntensity(1.0F);
      sky.SetLowerHemisphereIsSolidColor(false);
      auto& fog
        = *scene->GetEnvironment()->TryGetSystem<scene::environment::Fog>();
      fog.SetEnableVolumetricFog(false);
      fog.SetVisibleInRealTimeSkyCaptures(true);
      settings.manual_ev = 12.0F;
      scene->GetEnvironment()
        ->TryGetSystem<scene::environment::PostProcessVolume>()
        ->SetExposureSettings(settings);
      auto target = CreateRegisteredTexture({ .width = width,
        .height = height,
        .format = Format::kRGBA32Float,
        .debug_name = "IBL.Scene.Output",
        .is_render_target = true,
        .initial_state = graphics::ResourceStates::kCommon });
      framebuffer = Backend().CreateFramebuffer(
        graphics::FramebufferDesc {}.AddColorAttachment(target));
      frame.SetScene(observer_ptr { scene.get() });
      auto timing = frame.GetModuleTimingData();
      timing.game_delta_time
        = time::CanonicalDuration { std::chrono::nanoseconds { 16'666'667 } };
      frame.SetModuleTimingData(
        timing, engine::internal::EngineTagFactory::Get());
      probe->prepare = [](RenderContext& context) {
        context.current_view.depth_prepass_mode
          = DepthPrePassMode::kOpaqueAndMasked;
        context.current_view.with_atmosphere = true;
        context.current_view.with_height_fog = true;
      };
      auto& diagnostics = renderer_->GetDiagnosticsService();
      diagnostics.SetHdrPrecisionControl(HdrPrecisionControl::kProduction);
      diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
      diagnostics.SetGpuTimelineMaxScopesPerFrame(2048U);
      diagnostics.SetGpuTimelineRetainLatestFrame(false);
      diagnostics.SetGpuTimelineEnabled(true);
      auto cpu = std::ofstream(directory / "frames.csv");
      cpu << "frame_seq,cpu_frame_ms,revision,source_age,completion_frames,"
             "storage_creations,allocated_slots,registered_resources,draws\n";
      auto memory = nlohmann::json::array();
      const auto snapshot = [&]() {
        auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
        auto& ibl = RendererPublicationProbe::IblOwner(*owner);
        const auto stats = ibl.GetActivePoolStats();
        const auto counts = FailureBackend().resource_creations.Snapshot();
        CHECK_F(counts.has_value());
        memory.push_back({ { "frame_seq", sequence },
          { "storage_creations", stats.storage_creations },
          { "allocated_slots", stats.allocated },
          { "registered_resources",
            Backend().GetResourceRegistry().GetRegisteredResourceCount() },
          { "created_buffers", counts->buffers },
          { "created_textures", counts->textures },
          { "resources", FailureBackend().MeasureTrackedPlacement() } });
      };
      auto source_frames = std::unordered_map<std::uint64_t, unsigned> {};
      unsigned previous_revision = 0U;
      double first_use_wall_ms = 0.0;
      const auto paced_begin = Clock::now();
      for (unsigned index = 0U; index < warmup + samples + 4U; ++index) {
        const auto start = Clock::now();
        const auto slot
          = frame::Slot { sequence % frame::kFramesInFlight.get() };
        const auto current = frame::SequenceNumber { ++sequence };
        Backend().BeginFrame(current, slot);
        frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
        frame.SetFrameSequenceNumber(
          current, engine::internal::EngineTagFactory::Get());
        renderer_->OnFrameStart(observer_ptr { &frame });
        {
          const ScopeGuard finish([&]() noexcept {
            renderer_->OnFrameEnd(observer_ptr { &frame });
            Backend().EndFrame(current, slot);
          });
          const auto phase = mode == Mode::kStatic ? 0U : index;
          const auto angle = 0.2F + float(phase) * 0.0003F;
          ASSERT_TRUE(recipe.sun.GetTransform().SetLocalRotation(glm::quat(
            space::move::Forward,
            glm::normalize(glm::vec3(std::cos(angle), 0, -std::sin(angle))))));
          ASSERT_TRUE(second.GetTransform().SetLocalRotation(
            glm::quat(space::move::Forward,
              glm::normalize(glm::vec3(-0.4F, 0.1F, -0.5F)))));
          fog.SetFogDensity(0.001F + float(phase) * 0.0000001F);
          if (mode == Mode::kAuthoring)
            scene->NotifyEnvironmentAuthoringChange();
          scene->Update();
          scene->SyncObservers();
          if (index == 0U)
            ASSERT_TRUE(diagnostics.RequestGpuTimelineRecording(
              directory / "first-use-gpu.json", 1U));
          if (index == warmup)
            ASSERT_TRUE(diagnostics.RequestGpuTimelineRecording(
              directory / "gpu.json", samples));
          probe->draws = 0U;
          probe->color.reset();
          auto input = CompositionView::ForScene(ViewId { 100U }, view, camera);
          input.view_state_handle = CompositionView::ViewStateHandle { 100U };
          input.render_settings.exposure = settings;
          ASSERT_NE(
            renderer_->PublishRuntimeCompositionView(frame,
              { .composition_view = input,
                .render_target = observer_ptr { framebuffer.get() },
                .composite_source = observer_ptr { framebuffer.get() } },
              ShadingMode::kDeferred),
            kInvalidViewId);
          auto loop = co::testing::TestEventLoop {};
          // Run finishes while its coroutine closure remains alive.
          // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
          co::Run(loop, [&]() -> co::Co<void> {
            co_await renderer_->OnPreRender(observer_ptr { &frame });
            co_await renderer_->OnRender(observer_ptr { &frame });
            co_await renderer_->OnCompositing(observer_ptr { &frame });
          });
        }
        const auto elapsed
          = std::chrono::duration<double, std::milli>(Clock::now() - start)
              .count();
        if (index >= warmup) {
          ASSERT_GT(probe->draws, 0U);
          ASSERT_NE(probe->color, nullptr);
        }
        auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
        ASSERT_NE(owner, nullptr);
        auto& environment = RendererPublicationProbe::EnvironmentOwner(*owner);
        auto& ibl = RendererPublicationProbe::IblOwner(*owner);
        const auto& state = environment.InspectProbeState();
        ASSERT_TRUE(state.valid);
        const auto& stable = environment.InspectAtmosphereState();
        ASSERT_EQ(stable.view_products.atmosphere_light_count, 2U);
        ASSERT_TRUE(
          stable.view_products.height_fog.visible_in_real_time_sky_captures);
        source_frames.try_emplace(
          environment::internal::HashSkyCaptureInputs(stable), sequence);
        const auto source_frame
          = source_frames.at(state.static_sky_light.key.source_revision);
        const auto product = ibl.GetPublishedProducts();
        ASSERT_TRUE(product);
        const auto changed = product->revision != previous_revision;
        const auto age = mode == Mode::kStatic ? 0U : sequence - source_frame;
        const auto completed = changed ? age + 1U : 0U;
        previous_revision = product->revision;
        if (mode == Mode::kAuthoring) {
          EXPECT_TRUE(changed);
          EXPECT_EQ(age, 0U);
        }
        if (index >= warmup && index < warmup + samples) {
          const auto stats = ibl.GetActivePoolStats();
          cpu << sequence << ',' << elapsed << ',' << product->revision << ','
              << age << ',' << completed << ',' << stats.storage_creations
              << ',' << stats.allocated << ','
              << Backend().GetResourceRegistry().GetRegisteredResourceCount()
              << ',' << probe->draws << '\n';
        }
        if (index == 0U) {
          WaitForQueueIdle();
          first_use_wall_ms
            = std::chrono::duration<double, std::milli>(Clock::now() - start)
                .count();
        }
        if (index == warmup - 1U || index == warmup + samples - 1U) {
          WaitForQueueIdle();
          snapshot();
        }
        std::this_thread::sleep_until(paced_begin
          + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(double(index + 1U) / 60.0)));
      }
      WaitForQueueIdle();
      const auto pixels = ReadFloatTexture(*probe->color, true);
      ASSERT_EQ(pixels.size(), width * height);
      for (const auto& pixel : pixels) {
        for (unsigned channel = 0U; channel < 3U; ++channel) {
          ASSERT_TRUE(std::isfinite(pixel.at(channel)));
          ASSERT_GE(pixel.at(channel), 0.0F);
        }
      }
      auto image = std::ofstream(directory / "scene.rgba32f", std::ios::binary);
      // Preserve HDR pixels outside the timed window.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      image.write(reinterpret_cast<const char*>(pixels.data()),
        static_cast<std::streamsize>(pixels.size() * sizeof(Pixel)));
      ASSERT_TRUE(image.good());
      auto manifest = std::ofstream(directory / "run.json");
      manifest << nlohmann::json {
        { "mode",
          mode == Mode::kStatic      ? "static"
            : mode == Mode::kRuntime ? "runtime"
                                     : "authoring" },
        { "width", width }, { "height", height }, { "warmup", warmup },
        { "samples", samples }, { "hz", 60 },
        { "scene",
          "MultiView mixed scene; fixed camera; two atmosphere lights; "
          "captured "
          "height fog; deferred and translucent shading with shadows" },
        { "manual_ev", settings.manual_ev },
        { "first_use_wall_ms", first_use_wall_ms }, { "memory", memory }
      }.dump(2) << '\n';
    }
  }
} // namespace
} // namespace oxygen::vortex::testing::exposure
