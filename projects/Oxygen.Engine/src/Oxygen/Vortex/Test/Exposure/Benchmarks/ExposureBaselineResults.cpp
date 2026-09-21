//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <string>
#include <tuple>
#include <unordered_set>

#include <glm/trigonometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBaselineScenario.h>
#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureCpuTiming.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::ResourceStates;

auto ExposureBaselineScenario::SaveEndpoint(const unsigned path_frame) -> void
{
  fixture_.WaitForQueueIdle();
  for (unsigned index = 0U; index < view_count; ++index) {
    const auto& extract = endpoint_hdr.at(index);
    CHECK_NOTNULL_F(extract.texture);
    CHECK_NOTNULL_F(endpoint_exposure.at(index).get());
    const auto domain = fixture_.Read<FrameExposureData>(
      *endpoint_exposure.at(index)->buffer, ResourceStates::kShaderResource);
    const auto state = fixture_.Read<ExposureStateData>(
      *endpoint_exposure.at(index)->current_state->buffer,
      ResourceStates::kShaderResource);
    const auto status = fixture_.Read<ExposureCompletedStatus>(
      *endpoint_exposure.at(index)->current_state->status_buffer,
      ResourceStates::kCopySource);
    if (fp32_only) {
      CHECK_F(
        domain.pre_exposure == 1.0F && domain.one_over_pre_exposure == 1.0F);
      CHECK_F(extract.fallback == nullptr);
      CHECK_F(endpoint_exposure.at(index)->suitability_buffer == nullptr);
    }
    const auto* hdr_texture = extract.texture;
    auto used_fallback = false;
    if (extract.fallback != nullptr) {
      CHECK_NOTNULL_F(extract.exposure.get());
      const auto report = fixture_.Read<HdrSuitabilityData>(
        *extract.exposure->conversion_buffer, ResourceStates::kShaderResource);
      constexpr auto scene_product_mask = 1U << 10U;
      const auto accepted = report.failure_flags == 0U
        && report.checked_products == scene_product_mask
        && report.expected_products == scene_product_mask;
      if (!accepted) {
        hdr_texture = extract.fallback;
        used_fallback = true;
      }
    }
    const auto hdr = fixture_.ReadFloatTexture(*hdr_texture, true);
    const auto pixels = fixture_.ReadFloatTexture(
      *targets.at(index)->GetDescriptor().color_attachments.front().texture);
    auto scene_luminance = 0.0;
    auto display_luminance = 0.0;
    CHECK_F(hdr.size() == pixels.size() && !pixels.empty());
    for (std::size_t pixel = 0U; pixel < pixels.size(); ++pixel) {
      for (unsigned channel = 0U; channel < 3U; ++channel) {
        CHECK_F(std::isfinite(hdr.at(pixel).at(channel)));
        CHECK_F(std::isfinite(pixels.at(pixel).at(channel)));
      }
      const auto luminance = [](const Pixel& value) -> double {
        return (.2126 * value.at(0)) + (.7152 * value.at(1))
          + (.0722 * value.at(2));
      };
      scene_luminance
        += luminance(hdr.at(pixel)) * domain.one_over_pre_exposure;
      display_luminance += luminance(pixels.at(pixel));
    }
    scene_luminance /= static_cast<double>(pixels.size());
    display_luminance /= static_cast<double>(pixels.size());
    CHECK_F(scene_luminance > 0.0 && display_luminance > 0.0);
    const auto filename = stem + "-endpoint-" + std::to_string(path_frame)
      + "-view-" + std::to_string(index) + ".rgba32f";
    const auto path = directory / filename;
    CHECK_F(!std::filesystem::exists(path));
    auto output = std::ofstream(path, std::ios::binary);
    CHECK_F(output.is_open());
    // ostream::write accepts char bytes; Pixel storage is serialized unchanged.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char*>(pixels.data()),
      static_cast<std::streamsize>(pixels.size() * sizeof(Pixel)));
    output.close();
    CHECK_F(output.good());
    endpoints.push_back({
      {
        "displayed_scale",
        state.displayed_scale,
      },
      {
        "status_flags",
        status.flags,
      },
      {
        "first_failure_product",
        status.first_failure_product,
      },
      {
        "first_failure_kind",
        status.first_failure_kind,
      },
      {
        "fp16_eligible_streak",
        status.fp16_eligible_streak,
      },
      {
        "file",
        filename,
      },
      {
        "view_index",
        index,
      },
      {
        "width",
        width >> index,
      },
      {
        "height",
        height >> index,
      },
      {
        "path_phase",
        path_phases.at(index),
      },
      {
        "frame_sequence",
        fixture_.sequence,
      },
      {
        "pre_exposure",
        domain.pre_exposure,
      },
      {
        "used_fp32_fallback",
        used_fallback,
      },
      {
        "mean_scene_luminance",
        scene_luminance,
      },
      {
        "mean_display_luminance",
        display_luminance,
      },
      {
        "encoding",
        "Little-endian float32 RGBA, row-major; final renderer output, no "
        "additional normalization",
      },
    });
    endpoint_hdr.at(index) = {};
    endpoint_exposure.at(index).reset();
  }
}

auto ExposureBaselineScenario::CaptureEndpoints() -> void
{
  // Resolve the last recording outside the declared sample population. This
  // explicit drain is not part of the timed renderer loop or a frame-rate
  // claim.
  fixture_.WaitForQueueIdle();
  capture_endpoint = true;
  finalization = RenderFrame(false, sample_count);
  backend.track_resources = false;
  endpoints = nlohmann::json::array();
  SaveEndpoint(0U);
  capture_endpoint = false;
  if (moving) {
    for (unsigned path_frame = 1U; path_frame <= 600U; ++path_frame) {
      capture_endpoint = path_frame == 600U;
      std::ignore = RenderFrame(false, path_frame);
    }
    SaveEndpoint(600U);
  }
  fixture_.probe->inspect = {};
}

auto ExposureBaselineScenario::WriteCpuSamples() -> void
{
  auto cpu = std::ofstream(cpu_path, std::ios::binary);
  ASSERT_TRUE(cpu.is_open());
  cpu << std::setprecision(17)
      << "frame_seq,simulation_dt_ns,wall_ms,frame_start_ms,submission_ms,"
         "main_format,secondary_format,main_draws,secondary_draws,"
         "main_path_phase,secondary_path_phase,main_history_reprojected,"
         "secondary_history_reprojected,main_history_reset,secondary_history_"
         "reset\n";
  for (const auto& sample : samples) {
    cpu << sample.frame_sequence << ',' << simulation_dt_ns << ','
        << sample.wall_ms << ',' << sample.frame_start_ms << ','
        << sample.submission_ms << ',' << sample.formats.at(0) << ','
        << sample.formats.at(1) << ',' << sample.draws.at(0) << ','
        << sample.draws.at(1) << ',' << sample.path_phases.at(0) << ','
        << sample.path_phases.at(1) << ',' << sample.history_reprojected.at(0)
        << ',' << sample.history_reprojected.at(1) << ','
        << sample.history_reset.at(0) << ',' << sample.history_reset.at(1)
        << '\n';
  }
  cpu.close();
  ASSERT_TRUE(cpu.good());
}

auto ExposureBaselineScenario::WriteAndValidateResults() -> void
{
  ASSERT_NO_FATAL_FAILURE(WriteCpuSamples());
  auto gpu_stream = std::ifstream(gpu_path);
  ASSERT_TRUE(gpu_stream.is_open());
  const auto gpu = nlohmann::json::parse(gpu_stream);
  const auto adapter = backend.GetCurrentDevice()->GetAdapterLuid();
  const auto* const certified_precision_scope = fp32_reference
    ? "Format-only FP32 control; certification remains enabled"
    : "Production admission; certification remains enabled";
  const auto* const precision_scope = fp32_only
    ? "FP32-only control; P=1, exposure and range protection active, no FP16 "
      "admission"
    : certified_precision_scope;
  const char* recipe
    = "Emissive triangle 0.25, vacuum atmosphere, zero-extinction fog";
  if (moving) {
    recipe = "MultiView mixed exposure with six-piece sun-shadowed enclosure";
  } else if (mixed_scene) {
    recipe = "MultiView mixed exposure";
  }
  const auto manifest = nlohmann::json {
    { "schema_version", 1 },
    { "workload", workload },
    { "run_id", run_id },
    { "configuration", "Release" },
    { "width", width },
    { "height", height },
    { "view_count", view_count },
    { "prepared_draw_counts", warm_draw_counts },
    { "secondary_width", view_count == 2U ? width / 2U : 0U },
    { "secondary_height", view_count == 2U ? height / 2U : 0U },
    {
      "view_ids",
      view_count == 2U ? std::vector { 500, 501 } : std::vector { 500 },
    },
    {
      "view_state_handles",
      view_count == 2U ? std::vector { 500, 501 } : std::vector { 500 },
    },
    { "shading", forward ? "Forward" : "Deferred" },
    {
      "exposure",
      {
        {
          "mode",
          fixture_.settings.mode == engine::ExposureMode::kAuto ? "Auto"
                                                                : "Manual",
        },
        { "manual_ev", fixture_.settings.manual_ev },
        { "key", fixture_.settings.key },
        { "metering", "Average" },
        { "min_ev", fixture_.settings.min_ev },
        { "max_ev", fixture_.settings.max_ev },
        { "speed_up", fixture_.settings.speed_up },
        { "speed_down", fixture_.settings.speed_down },
      },
    },
    { "precision", precision },
    { "precision_scope", precision_scope },
    { "recipe", recipe },
    {
      "camera_path",
      moving ? "1200 frames: 300 exterior hold, 300 smoothstep entry, 300 "
               "interior hold, 300 smoothstep exit; secondary phase +600"
             : "Static",
    },
    {
      "camera_aspect",
      mixed_scene ? static_cast<double>(width) / height : 1.0,
    },
    {
      "camera_fov_radians",
      mixed_scene ? static_cast<double>(glm::radians(45.0F)) : 1.0,
    },
    { "tone_mapper", "None" },
    { "display_gamma", 1 },
    { "quality_commands", quality_commands },
    { "temporal_fog", temporal },
    { "jitter", false },
    { "simulation_dt_ns", simulation_dt_ns },
    { "frame_slots", 3 },
    { "warmup_frames", warm_frames },
    { "warmup_seconds", warm_seconds },
    { "sample_count", sample_count },
    { "sample_seconds", sample_seconds },
    { "first_frame_seq", samples.front().frame_sequence },
    { "last_frame_seq", samples.back().frame_sequence },
    { "adapter_luid_low", adapter.LowPart },
    { "adapter_luid_high", adapter.HighPart },
    { "cpu_samples", cpu_path.filename().string() },
    { "logging_verbosity", loguru::g_global_verbosity },
    {
      "cpu_owner_timing",
      cpu_timing ? cpu_timing->Save(directory / (stem + ".cpu-owners.csv"))
                 : nlohmann::json(nullptr),
    },
    { "gpu_samples", gpu_path.filename().string() },
    { "gpu_complete", gpu.at("complete") },
    { "gpu_timing_valid", gpu.at("timing_valid") },
    { "resources_before", before },
    { "resources_after", after },
    {
      "resource_scope",
      "Resources created after fixture setup, including "
      "outputs; native placement requirements, not committed heap residency. "
      "No retained extracts beyond normal renderer/probe ownership.",
    },
    { "finalization_frame_seq", finalization.frame_sequence },
    { "finalization_wall_ms", finalization.wall_ms },
    { "untimed_endpoint_images", endpoints },
    { "acceptance_windows", acceptance_windows },
    {
      "scope",
      "Native offscreen workload; no presented FPS claim. "
      "Frame-start duration includes backend waits; submission is a "
      "CPU/driver/recording span, not pure active CPU time. Correctness "
      "readbacks and explicit drains are absent from measured frames. "
      "Source/binary/shader hashes and clock/thermal samples belong to the "
      "external frozen-checkpoint runner.",
    },
  };
  auto output = std::ofstream(manifest_path, std::ios::binary);
  ASSERT_TRUE(output.is_open());
  output << manifest.dump(2) << '\n';
  output.close();
  ASSERT_TRUE(output.good());
  oxygen::vortex::testing::exposure::ExposureProfilingOverheadTest::
    RecordProperty("baseline_manifest", manifest_path.string());
  EXPECT_GE(sample_seconds, 30.0);
  EXPECT_EQ(gpu.at("complete"), true);
  EXPECT_EQ(gpu.at("timing_valid"), true);
  EXPECT_EQ(gpu.at("first_frame_seq"), samples.front().frame_sequence);
  ASSERT_EQ(gpu.at("frames").size(), sample_count);
  auto frame_ids = std::unordered_set<unsigned> {};
  for (const auto& measured : gpu.at("frames")) {
    EXPECT_TRUE(
      frame_ids.insert(measured.at("frame_seq").get<unsigned>()).second);
  }
  for (const auto& sample : samples) {
    EXPECT_TRUE(frame_ids.contains(sample.frame_sequence));
  }
}

} // namespace oxygen::vortex::testing::exposure
