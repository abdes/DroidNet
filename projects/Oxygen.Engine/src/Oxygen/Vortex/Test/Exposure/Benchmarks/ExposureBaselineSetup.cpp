//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <stdexcept>

#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBaselineScenario.h>
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
  ASSERT_TRUE(precision == "production" || precision == "fp32")
    << "OXYGEN_EXPOSURE_BASELINE_PRECISION must be production or fp32";
  fp32_reference = precision == "fp32";
  width_text = option("OXYGEN_EXPOSURE_TIMING_WIDTH", "1920");
  ASSERT_TRUE(width_text == "1920" || width_text == "3840")
    << "OXYGEN_EXPOSURE_TIMING_WIDTH must be 1920 or 3840";
  const auto frames_text = option("OXYGEN_EXPOSURE_BASELINE_FRAMES", "3600");
  sample_count = 0U;
  const auto parsed = std::from_chars(
    frames_text.data(), std::to_address(frames_text.end()), sample_count);
  ASSERT_TRUE(parsed.ec == std::errc {}
    && parsed.ptr == std::to_address(frames_text.end()) && sample_count >= 1800U
    && sample_count <= 20000U)
    << "OXYGEN_EXPOSURE_BASELINE_FRAMES must be an integer in [1800, 20000]";
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
  expected_format
    = fp32_reference || temporal ? Format::kRGBA32Float : Format::kRGBA16Float;
  directory = std::filesystem::path {
    OXYGEN_EXPOSURE_WORKSPACE,
  } / "out/build-ninja/analysis/vortex/exposure-lightbench/slice51";
  std::filesystem::create_directories(directory);
  stem = filename_prefix + workload + "-" + width_text
    + (fp32_reference ? "-fp32" : "") + "-" + run_id + "-Release";
  gpu_path = directory / (stem + ".gpu.json");
  cpu_path = directory / (stem + ".cpu.csv");
  manifest_path = directory / (stem + ".json");
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
  diagnostics.SetHdrFp32ReferenceEnabled(fp32_reference);
  diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
  diagnostics.SetGpuTimelineEnabled(true);
  fixture_.probe->inspect
    = [this](const RenderContext& context, const SceneTextureExtractRef& color,
        unsigned draws) -> void { InspectView(context, color, draws); };
}

} // namespace oxygen::vortex::testing::exposure
