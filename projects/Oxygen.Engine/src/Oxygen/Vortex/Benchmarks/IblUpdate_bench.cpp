//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Windows.h> // IWYU pragma: keep

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <minwindef.h>
#include <nlohmann/json.hpp>
#include <processenv.h>
#include <winerror.h>
#include <winreg.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/CapturedSkySource.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/Environment/Types/IblProductMetadata.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Upload/UploadPlanner.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  [[nodiscard]] auto ReadEnvironmentVariable(const wchar_t* name)
    -> std::wstring
  {
    const auto capacity = GetEnvironmentVariableW(name, nullptr, 0U);
    if (capacity == 0U) {
      return {};
    }
    auto value = std::wstring(capacity, L'\0');
    const auto length = GetEnvironmentVariableW(name, value.data(), capacity);
    if (length >= capacity) {
      throw std::runtime_error("Environment variable changed during read");
    }
    value.resize(length);
    return value;
  }

  class IblUpdateBenchmark : public ExposureGpuTest {
  protected:
    auto RunUpdates(bool scheduled, bool authoring = false,
      unsigned specified_face_size = 0U) -> void;
    struct SpecifiedSource {
      std::shared_ptr<graphics::Texture> texture;
      graphics::RegistrationLease registration;
    };
    auto MakeSpecifiedSource(unsigned size) -> SpecifiedSource
    {
      using graphics::ResourceStates;
      using Pixel = std::array<float, 4>;
      auto texture = Backend().CreateTexture({ .width = size,
        .height = size,
        .array_size = 6U,
        .format = Format::kRGBA32Float,
        .texture_type = TextureType::kTextureCube,
        .debug_name = "IBL.ScalingSource",
        .is_shader_resource = true,
        .initial_state = ResourceStates::kCommon });
      CHECK_NOTNULL_F(texture.get());
      auto registration
        = Backend().GetResourceRegistry().RegisterManaged(texture);
      CHECK_F(registration.has_value());
      auto subresources = std::array<upload::UploadSubresource, 6> {};
      for (auto face = 0U; face < 6U; ++face)
        subresources[face].array_slice = face;
      const auto plan = upload::UploadPlanner::PlanTexture2D({ .dst = texture },
        subresources, upload::UploadPolicy { QueueKeyFor() });
      CHECK_F(plan.has_value());
      auto staging = CreateRegisteredBuffer({ .size_bytes = plan->total_bytes,
        .memory = graphics::BufferMemory::kUpload,
        .debug_name = "IBL.ScalingUpload" });
      auto row = std::vector<Pixel>(size);
      for (auto face = 0U; face < 6U; ++face) {
        const auto& region = plan->regions[face];
        for (auto y = 0U; y < size; ++y) {
          for (auto x = 0U; x < size; ++x)
            row[x] = { 0.2F + 0.6F * x / size, 0.3F + 0.3F * y / size,
              0.5F + 0.01F * face, 1.0F };
          staging->Update(row.data(), row.size() * sizeof(Pixel),
            region.buffer_offset + std::uint64_t(y) * region.buffer_row_pitch);
        }
      }
      SubmitCommands(
        "IBL scaling upload", [&](graphics::CommandRecorder& recorder) {
          CHECK_F(static_cast<bool>(recorder.RetainRegistration(
            Backend().GetResourceRegistry(), *registration)));
          EnsureTracked(recorder, staging, ResourceStates::kGenericRead);
          recorder.BeginTrackingResourceState(
            *texture, ResourceStates::kCommon);
          recorder.RequireResourceState(*texture, ResourceStates::kCopyDest);
          recorder.FlushBarriers();
          for (const auto& region : plan->regions)
            recorder.CopyBufferToTexture(*staging, region, *texture);
          recorder.RequireResourceStateFinal(
            *texture, ResourceStates::kShaderResource);
        });
      return { texture, std::move(*registration) };
    }
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
    RunUpdates(false);
  }

  NOLINT_TEST_F(IblUpdateBenchmark, DISABLED_ScheduledSunUpdates)
  {
    RunUpdates(true);
  }

  NOLINT_TEST_F(IblUpdateBenchmark, DISABLED_AuthoringSunUpdates)
  {
    RunUpdates(true, true);
  }

  NOLINT_TEST_F(IblUpdateBenchmark, DISABLED_SpecifiedCubeUpdates)
  {
    const auto value = ReadEnvironmentVariable(L"OXYGEN_IBL_FACE_SIZE");
    ASSERT_FALSE(value.empty());
    const auto size = std::stoul(value);
    ASSERT_TRUE(size == 64U || size == 128U || size == 256U || size == 512U);
    RunUpdates(false, false, static_cast<unsigned>(size));
  }

  auto IblUpdateBenchmark::RunUpdates(const bool scheduled,
    const bool authoring, const unsigned specified_face_size) -> void
  {
#ifndef NDEBUG
    GTEST_SKIP() << "Native timing requires Release";
#endif
    const auto output = ReadEnvironmentVariable(L"OXYGEN_IBL_TIMING_OUTPUT");
    ASSERT_FALSE(output.empty());
    const auto directory = std::filesystem::path(output);
    ASSERT_FALSE(std::filesystem::exists(directory));
    std::filesystem::create_directories(directory);
    const bool stable_power
      = ReadEnvironmentVariable(L"OXYGEN_IBL_STABLE_POWER") == L"1";
    if (stable_power) {
      DWORD developer_mode {};
      DWORD size = sizeof(developer_mode);
      ASSERT_EQ(
        RegGetValueW(HKEY_LOCAL_MACHINE,
          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock",
          L"AllowDevelopmentWithoutDevLicense", RRF_RT_REG_DWORD, nullptr,
          &developer_mode, &size),
        ERROR_SUCCESS);
      ASSERT_NE(developer_mode, 0U)
        << "Stable GPU power requires Windows Developer Mode";
      ASSERT_TRUE(
        SUCCEEDED(Backend().GetCurrentDevice()->SetStablePowerState(TRUE)));
    }
    const ScopeGuard restore_power([&] noexcept -> void {
      if (stable_power) {
        CHECK_F(
          SUCCEEDED(Backend().GetCurrentDevice()->SetStablePowerState(FALSE)));
      }
    });
    const bool specified = specified_face_size != 0U;
    FailureBackend().SetRecorderNameCollectionEnabled(false);
    if (specified)
      FailureBackend().track_resources = true;
    auto memory = nlohmann::json::array();
    auto& diagnostics = renderer_->GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
    diagnostics.SetGpuTimelineMaxScopesPerFrame(scheduled ? 1024U : 128U);
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
    auto scheduler
      = scheduled ? std::make_unique<env::IblProcessor>(*renderer_) : nullptr;
    using Clock = std::chrono::steady_clock;
    const auto first_begin = Clock::now();
    std::shared_ptr<const env::IblBrdfProduct> brdf;
    if (!scheduled) {
      const auto prepared = brdf_owner.Prepare();
      ASSERT_TRUE(prepared);
      brdf = *prepared;
    }
    const auto cube = specified ? MakeSpecifiedSource(specified_face_size)
                                : SpecifiedSource {};
    auto state = env::StableAtmosphereState {};
    state.atmosphere_revision = 1U;
    state.view_products.atmosphere.enabled = true;
    state.view_products.atmosphere_light_count = scheduled ? 2U : 1U;
    state.view_products.sky_light.enabled = true;
    state.view_products.sky_light.source
      = environment::kSkyLightSourceCapturedScene;
    if (scheduled) {
      auto& secondary = state.view_products.atmosphere_lights[1];
      secondary.enabled = true;
      secondary.illuminance_rgb_lux = { 1000, 2000, 4000 };
      secondary.disk_luminance_scale_rgb = glm::vec3(1.0F);
      state.view_products.height_fog.enabled = true;
      state.view_products.height_fog.fog_density = 0.01F;
      state.view_products.height_fog.fog_height_falloff = 0.001F;
      state.view_products.height_fog.fog_inscattering_luminance
        = { 0.2F, 0.3F, 0.4F };
    }
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
    cpu << (scheduled
        ? "frame_seq,cpu_record_ms,published_revision,source_age,completion_"
          "frames,feedback_samples\n"
        : "frame_seq,cpu_record_ms,storage_creations,allocated_slots\n");
    auto source_frames = std::unordered_map<std::uint64_t, unsigned> {};
    const auto paced_begin = Clock::now();
    for (unsigned index = 0; index < warmup + samples + 4U; ++index) {
      const auto sequence = frame::SequenceNumber { index + 1U };
      const auto slot = frame::Slot { index % frame::kFramesInFlight.get() };
      Backend().BeginFrame(sequence, slot);
      frame.SetFrameSequenceNumber(
        sequence, engine::internal::EngineTagFactory::Get());
      frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
      renderer_->OnFrameStart(observer_ptr { &frame });
      if (scheduler)
        static_cast<void>(scheduler->OnFrameStart(sequence));
      if (index == 0U && scheduled) {
        ASSERT_TRUE(diagnostics.RequestGpuTimelineRecording(
          directory / "first-use-gpu.json", 1U));
      }
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
      if (authoring)
        state.authoring_revision = index + 1U;
      if (scheduled) {
        state.view_products.atmosphere_lights[1].direction_to_light_ws
          = -light.direction_to_light_ws;
        source_frames.emplace(env::HashSkyCaptureInputs(state), index + 1U);
      }
      const auto begin = Clock::now();
      unsigned source_age = 0U;
      unsigned completion_frames = 0U;
      if (scheduled) {
        const auto next
          = scheduler->RefreshSkyLightProducts({}, ctx_, state, fog, {});
        ASSERT_TRUE(next.probe_state.valid);
        published = scheduler->GetPublishedProducts();
        ASSERT_TRUE(published);
        const auto source_frame = source_frames.at(
          next.probe_state.static_sky_light.key.source_revision);
        source_age = index + 1U - source_frame;
        completion_frames = next.refreshed ? source_age + 1U : 0U;
        if (authoring) {
          EXPECT_TRUE(next.refreshed);
          EXPECT_EQ(source_age, 0U);
          EXPECT_EQ(completion_frames, 1U);
        }
      } else if (specified) {
        auto next = processor.Process(cube.texture, cube.registration, brdf,
          { .face_size = specified_face_size,
            .lower_hemisphere_solid_color = false },
          index + 1U);
        ASSERT_TRUE(next.has_value()) << static_cast<int>(next.error());
        published = std::move(*next);
      } else {
        auto next
          = source.Process(ctx_, state, fog, processor, brdf, {}, index + 1U);
        ASSERT_TRUE(next.has_value()) << static_cast<int>(next.error());
        published = std::move(*next);
      }
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
        cpu << index + 1U << ',' << elapsed << ',';
        if (scheduled)
          cpu << published->revision << ',' << source_age << ','
              << completion_frames << ',' << scheduler->GetTimingSampleCount()
              << '\n';
        else {
          const auto stats = processor.GetStats();
          cpu << stats.storage_creations << ',' << stats.allocated << '\n';
        }
      }
      if (specified
        && (index + 1U == warmup || index + 1U == warmup + samples)) {
        memory.push_back({ { "frame_seq", index + 1U },
          { "storage_creations", processor.GetStats().storage_creations },
          { "allocated_slots", processor.GetStats().allocated },
          { "registered_resources",
            Backend().GetResourceRegistry().GetRegisteredResourceCount() },
          { "resources", FailureBackend().MeasureTrackedPlacement() } });
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
      if (scheduled) {
        EXPECT_GE(process_scopes, 1U);
        EXPECT_LE(process_scopes, 2U);
      } else
        EXPECT_EQ(process_scopes, 1U);
    }
    if (specified) {
      auto readback
        = GetReadbackManager()->CreateBufferReadback("IBL scaling validity");
      SubmitCommands(
        "IBL scaling validity", [&](graphics::CommandRecorder& recorder) {
          CHECK_F(published->Attach(recorder, Backend().GetResourceRegistry()));
          recorder.FlushBarriers();
          CHECK_F(readback
              ->EnqueueCopy(recorder, *published->metadata,
                { 0U, sizeof(environment::IblProductMetadata) })
              .has_value());
        });
      auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      auto metadata = environment::IblProductMetadata {};
      std::memcpy(&metadata, mapped->Bytes().data(), sizeof(metadata));
      ASSERT_EQ(metadata.product_revision, published->revision);
      ASSERT_EQ(metadata.processing_flags,
        environment::kIblProductFinite | environment::kIblProductComplete);
      ASSERT_GT(metadata.average_brightness, 0.0F);
    }
    auto manifest = std::ofstream(directory / "run.json");
    manifest << nlohmann::json { { "samples", samples }, { "warmup", warmup },
      { "hz", 60 }, { "first_use_wall_ms", first_use_wall_ms },
      { "stable_power", stable_power }, { "scheduled", scheduled },
      { "authoring", authoring },
      { "specified_face_size", specified_face_size }, { "memory", memory },
      { "scope",
        specified
          ? "Isolated specified-cube immediate processing; size scaling report"
          : authoring
          ? "Isolated same-frame authoring updates with two atmosphere "
            "lights and height fog"
          : scheduled
          ? "Isolated automatic runtime updates with two atmosphere "
            "lights and height fog; native timings and source latency, "
            "no full-scene acceptance claim"
          : "Isolated immediate atmosphere+fog capture/convolution; "
            "full-scene acceptance remains open" } }
                  .dump(2)
             << '\n';
  }
} // namespace
} // namespace oxygen::vortex::testing::exposure
