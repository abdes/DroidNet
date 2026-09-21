//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <stdlib.h>
#include <string>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBaselineScenario.h>
#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBenchmarkFixture.h>
// Completes the unique_ptr pointee for the out-of-line constructor/destructor.
#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureCpuTiming.h> // IWYU pragma: keep
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/ExposureBenchmarkScene.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;

ExposureBaselineScenario::ExposureBaselineScenario(
  ExposureProfilingOverheadTest& fixture,
  const ExposureProfilingOverheadTest::BaselineRecipe kind)
  : fixture_(fixture)
  , backend(fixture.FailureBackend())
  , moving(
      kind == ExposureProfilingOverheadTest::BaselineRecipe::kIndoorOutdoor)
  , mixed_scene(
      kind != ExposureProfilingOverheadTest::BaselineRecipe::kControlled)
{
}

ExposureBaselineScenario::~ExposureBaselineScenario()
{
  fixture_.probe->inspect = {};
}

auto ExposureBaselineScenario::ReadOptions() -> void
{
  const auto option
    = [](const char* name, std::string fallback) -> std::string {
    char* value = nullptr;
    std::size_t size = 0U;
    const auto result = _dupenv_s(&value, &size, name);
    const auto owned
      = std::unique_ptr<char, decltype(&std::free)>(value, &std::free);
    if (result != 0) {
      throw std::runtime_error(std::string {
                                 "Cannot read ",
                               }
        + name);
    }
    return value ? std::string { value, } : std::move(fallback);
  };
  std::string default_workload = "C01";
  std::string filename_prefix = "controlled-";
  if (moving) {
    default_workload = "I01";
    filename_prefix = "indoor-";
  } else if (mixed_scene) {
    default_workload = "M01";
    filename_prefix = "mixed-";
  }
  workload = option("OXYGEN_EXPOSURE_BASELINE_CASE", default_workload);
  if (moving) {
    ASSERT_TRUE(workload == "I01" || workload == "I02")
      << "Indoor/outdoor baseline requires I01 or I02";
  } else if (mixed_scene) {
    ASSERT_TRUE(workload == "M01" || workload == "M02" || workload == "M03"
      || workload == "M04")
      << "Mixed baseline requires M01, M02, M03 or M04";
  } else {
    ASSERT_TRUE(workload == "C01" || workload == "C02")
      << "Controlled baseline requires C01 or C02";
  }
  precision = option("OXYGEN_EXPOSURE_BASELINE_PRECISION", "production");
  ASSERT_TRUE(precision == "production" || precision == "fp32"
    || precision == "fp32-only" || precision == "qualified")
    << "OXYGEN_EXPOSURE_BASELINE_PRECISION must be production, fp32 or "
       "fp32-only or qualified";
  fp32_reference = precision == "fp32";
  fp32_only = precision == "fp32-only" || precision == "production";
  width_text = option("OXYGEN_EXPOSURE_TIMING_WIDTH", "1920");
  ASSERT_TRUE(width_text == "1920" || width_text == "3840")
    << "OXYGEN_EXPOSURE_TIMING_WIDTH must be 1920 or 3840";
  const auto frames_text = option("OXYGEN_EXPOSURE_BASELINE_FRAMES", "3600");
  warmup_only = frames_text == "warmup";
  automatic_sample_count = frames_text == "auto";
  const auto acceptance_text
    = option("OXYGEN_EXPOSURE_BASELINE_ACCEPTANCE", "0");
  ASSERT_TRUE(acceptance_text == "0" || acceptance_text == "1");
  acceptance = acceptance_text == "1" && !warmup_only;
  const auto cpu_text = option("OXYGEN_EXPOSURE_BASELINE_CPU", "0");
  ASSERT_TRUE(cpu_text == "0" || cpu_text == "1");
  measure_cpu_owners = cpu_text == "1" && !warmup_only;
  const auto detail_text = option("OXYGEN_EXPOSURE_BASELINE_CPU_DETAIL", "0");
  ASSERT_TRUE(detail_text == "0" || detail_text == "1");
  measure_cpu_details = detail_text == "1" && !warmup_only;
  ASSERT_FALSE(measure_cpu_details && !measure_cpu_owners);
  if (measure_cpu_owners) {
    ASSERT_EQ(loguru::g_global_verbosity, loguru::Verbosity_OFF)
      << "CPU measurement requires all logging disabled with -v=OFF";
  }
  ASSERT_FALSE(acceptance && precision != "production");
  ASSERT_FALSE(automatic_sample_count && !acceptance);
  const auto measured_frames = warmup_only || automatic_sample_count
    ? std::string { "2400", } : frames_text;
  sample_count = 0U;
  const auto parsed = std::from_chars(measured_frames.data(),
    std::to_address(measured_frames.end()), sample_count);
  ASSERT_TRUE(parsed.ec == std::errc {}
    && parsed.ptr == std::to_address(measured_frames.end())
    && sample_count >= 1800U && sample_count <= 20000U)
    << "OXYGEN_EXPOSURE_BASELINE_FRAMES must be warmup or an integer in [1800, "
       "20000]";
  if (moving) {
    ASSERT_EQ(sample_count % 1200U, 0U)
      << "Indoor/outdoor measurements require complete 1200-frame cycles";
  }
  run_id = option("OXYGEN_EXPOSURE_BASELINE_RUN", "run01");
  ASSERT_TRUE(!run_id.empty() && run_id.size() <= 64U
    && std::ranges::all_of(run_id,
      [](const char c) -> bool {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
          || (c >= '0' && c <= '9') || c == '-' || c == '_';
      }))
    << "OXYGEN_EXPOSURE_BASELINE_RUN must contain 1-64 ASCII letters, digits, "
       "hyphens or underscores";
  ASSERT_TRUE(fixture_.CapturePath().empty())
    << "Native baseline measurements do not permit RenderDoc capture";
  width = width_text == "1920" ? 1920U : 3840U;
  height = width * 9U / 16U;
  temporal
    = moving || workload == "C02" || workload == "M02" || workload == "M04";
  forward = workload == "M03" || workload == "M04" || workload == "I02";
  view_count
    = workload == "M01" || workload == "M04" || workload == "I01" ? 1U : 2U;
  expected_format = fp32_reference || fp32_only || temporal
    ? Format::kRGBA32Float
    : Format::kRGBA16Float;
  directory = std::filesystem::path {
    OXYGEN_EXPOSURE_WORKSPACE,
  } / "out/build-ninja/analysis/vortex/exposure-lightbench/slice51";
  std::filesystem::create_directories(directory);
  stem = filename_prefix + workload + "-" + width_text
    + (precision == "production" ? "" : "-" + precision) + "-" + run_id
    + "-Release";
  gpu_path = directory / (stem + ".gpu.json");
  cpu_path = directory / (stem + ".cpu.csv");
  manifest_path = directory / (stem + ".json");
  recording_path = gpu_path;
  recording_frames = sample_count;
  startup.name = "startup";
  startup.gpu = directory / (stem + "-startup.gpu.json");
  matched.name = "matched";
  matched.gpu = directory / (stem + "-matched.gpu.json");
  events.name = "events";
  events.gpu = directory / (stem + "-events.gpu.json");
  ASSERT_FALSE(acceptance
    && (std::filesystem::exists(startup.gpu)
      || std::filesystem::exists(matched.gpu)
      || std::filesystem::exists(events.gpu)));
  ASSERT_FALSE(std::filesystem::exists(gpu_path)
    || std::filesystem::exists(cpu_path)
    || std::filesystem::exists(manifest_path))
    << "Choose a new run ID; existing baseline evidence is not overwritten";
}

auto ExposureBaselineScenario::ConfigureScene() -> void
{
  fixture_.view.viewport = {
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
  };
  cameras = std::array {
    fixture_.camera,
    fixture_.camera,
  };
  if (mixed_scene) {
    ASSERT_TRUE(fixture_.scene->DestroyNode(fixture_.mesh_node));
    ASSERT_TRUE(fixture_.scene->DestroyNode(fixture_.camera));
    const auto recipe = vortex::testing::PopulateMixedExposureBenchmarkScene(
      *fixture_.scene, moving);
    cameras = {
      recipe.main_camera,
      recipe.secondary_camera,
    };
    sun = recipe.sun;
    for (unsigned index = 0U; index < view_count; ++index) {
      const auto camera_lens
        = cameras.at(index).GetCameraAs<scene::PerspectiveCamera>();
      if (!camera_lens.has_value()) {
        FAIL() << "Expected a perspective camera";
      }
      auto& lens = camera_lens->get();
      auto viewport = fixture_.view.viewport;
      viewport.width = static_cast<float>(width >> index);
      viewport.height = static_cast<float>(height >> index);
      lens.SetViewport(viewport);
      lens.SetAspectRatio(
        static_cast<float>(width) / static_cast<float>(height));
    }
    fixture_.settings = scene::ExposureSettings {};
    fixture_.settings.key = 12.5F;
    if (workload == "M03") {
      fixture_.settings.mode = engine::ExposureMode::kManual;
      fixture_.settings.manual_ev = 14.5F;
    }
    fixture_.scene->GetEnvironment()
      ->TryGetSystem<scene::environment::PostProcessVolume>()
      ->SetExposureSettings(fixture_.settings);
  } else {
    // Preserve the historical controlled camera, including aspect 1 and FOV 1.
    const auto camera_lens
      = fixture_.camera.GetCameraAs<scene::PerspectiveCamera>();
    if (!camera_lens.has_value()) {
      FAIL() << "Expected a perspective camera";
    }
    auto& lens = camera_lens->get();
    ASSERT_FLOAT_EQ(lens.GetAspectRatio(), 1.0F);
    ASSERT_FLOAT_EQ(lens.GetFieldOfView(), 1.0F);
    lens.SetViewport(fixture_.view.viewport);
    fixture_.SetSurface(data::MaterialDomain::kOpaque, .25F);
    auto& sky = fixture_.scene->GetEnvironment()
                  ->AddSystem<scene::environment::SkyAtmosphere>();
    sky.SetEnabled(true);
    sky.SetRayleighScatteringRgb({
      0,
      0,
      0,
    });
    sky.SetMieScatteringRgb({
      0,
      0,
      0,
    });
    sky.SetMieAbsorptionRgb({
      0,
      0,
      0,
    });
    sky.SetOzoneAbsorptionRgb({
      0,
      0,
      0,
    });
    auto& fog
      = fixture_.scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
    fog.SetEnabled(true);
    fog.SetEnableHeightFog(true);
    fog.SetEnableVolumetricFog(true);
    fog.SetExtinctionSigmaTPerMeter(0);
  }
  ASSERT_EQ(
    fixture_.fixture_console
      .Execute(temporal ? "vtx.volumetric_fog.temporal_reprojection true"
                        : "vtx.volumetric_fog.temporal_reprojection false")
      .status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(
    fixture_.fixture_console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  for (const auto* command : quality_commands) {
    ASSERT_EQ(fixture_.fixture_console.Execute(command).status,
      console::ExecutionStatus::kOk)
      << command;
  }
  fixture_.probe->prepare = [](RenderContext& context) -> void {
    context.current_view.with_atmosphere = true;
    context.current_view.with_height_fog = true;
  };
  fixture_.scene->Update();
  fixture_.scene->SyncObservers();
}

auto ExposureBaselineScenario::CreateTargets() -> void
{
  targets.resize(view_count);
  for (unsigned index = 0U; index < targets.size(); ++index) {
    auto output = fixture_.CreateRegisteredTexture({ .width = width >> index,
      .height = height >> index,
      .format = Format::kRGBA32Float,
      .debug_name = std::string { mixed_scene ? "MixedBaseline.Output"
                                              : "ControlledBaseline.Output", }
        + std::to_string(index),
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon, });
    targets.at(index) = fixture_.Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
  }
}

auto ExposureBaselineScenario::Setup() -> void
{
  ASSERT_NO_FATAL_FAILURE(ReadOptions());
  ASSERT_NO_FATAL_FAILURE(ConfigureScene());
  backend.track_resources = true;
  backend.account_texture_allocations = false;
  ASSERT_NO_FATAL_FAILURE(CreateTargets());
  auto timing = fixture_.frame.GetModuleTimingData();
  timing.game_delta_time = time::CanonicalDuration {
    std::chrono::nanoseconds {
      simulation_dt_ns,
    },
  };
  fixture_.frame.SetModuleTimingData(
    timing, engine::internal::EngineTagFactory::Get());
  auto& diagnostics = fixture_.renderer_->GetDiagnosticsService();
  const auto certified_precision = fp32_reference
    ? HdrPrecisionControl::kFp32Reference
    : HdrPrecisionControl::kQualified;
  const auto non_production_precision
    = fp32_only ? HdrPrecisionControl::kFp32Only : certified_precision;
  const auto precision_control = precision == "production"
    ? HdrPrecisionControl::kProduction
    : non_production_precision;
  diagnostics.SetHdrPrecisionControl(precision_control);
  diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
  diagnostics.SetGpuTimelineEnabled(true);
  fixture_.probe->inspect
    = [this](const RenderContext& context, const SceneTextureExtractRef& color,
        unsigned draws) -> void { InspectView(context, color, draws); };
}

} // namespace oxygen::vortex::testing::exposure
