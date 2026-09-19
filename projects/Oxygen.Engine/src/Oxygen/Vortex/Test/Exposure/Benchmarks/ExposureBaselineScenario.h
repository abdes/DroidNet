//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <chrono>
#include <filesystem>

#include <nlohmann/json.hpp>

#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBenchmarkFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
namespace oxygen::vortex::testing::exposure {

// Owns one benchmark execution; timed rendering is separate from evidence I/O.
class ExposureBaselineScenario final {
public:
  ExposureBaselineScenario(ExposureProfilingOverheadTest& fixture,
    ExposureProfilingOverheadTest::BaselineRecipe kind);
  ~ExposureBaselineScenario();
  ExposureBaselineScenario(const ExposureBaselineScenario&) = delete;
  auto operator=(const ExposureBaselineScenario&)
    -> ExposureBaselineScenario& = delete;
  auto Run() -> void;

private:
  using Clock = std::chrono::steady_clock;
  struct Sample {
    unsigned frame_sequence {};
    double wall_ms {};
    double frame_start_ms {};
    double submission_ms {};
    std::array<unsigned, 2> formats {};
    std::array<unsigned, 2> draws {};
    std::array<unsigned, 2> path_phases {};
    std::array<unsigned, 2> history_reprojected {};
    std::array<unsigned, 2> history_reset {};
  };
  static auto Milliseconds(Clock::duration duration) -> double;
  auto Setup() -> void;
  auto ReadOptions() -> void;
  auto ConfigureScene() -> void;
  auto CreateTargets() -> void;
  auto UpdatePath(unsigned sample_frame) -> void;
  auto InspectView(const RenderContext& context,
    const SceneTextureExtractRef& color, unsigned draws) -> void;
  auto RenderFrame(bool start_recording, unsigned sample_frame = 0U) -> Sample;
  auto WarmUp() -> void;
  auto MeasureFrames() -> void;
  auto Snapshot() -> nlohmann::json;
  auto CaptureEndpoints() -> void;
  auto SaveEndpoint(unsigned path_frame) -> void;
  auto WriteCpuSamples() -> void;
  auto WriteAndValidateResults() -> void;

  static constexpr auto quality_commands = std::array {
    "vtx.sky_atmosphere.aerial_perspective_lut.width 64",
    "vtx.sky_atmosphere.aerial_perspective_lut.depth_resolution 32",
    "vtx.sky_atmosphere.aerial_perspective_lut.depth_km 96.0",
    "vtx.sky_atmosphere.aerial_perspective_lut.sample_count_max_per_slice 2.0",
    "vtx.volumetric_fog.history_miss_supersample_count 4",
    "vtx.volumetric_fog.directional_shadows true"
  };

  static constexpr auto simulation_dt_ns = 16'666'667;
  ExposureProfilingOverheadTest& fixture_;
  ExposureFailureGraphics& backend;
  const bool moving;
  const bool mixed_scene;
  std::string workload;
  std::string precision;
  std::string width_text;
  std::string run_id;
  std::string stem;
  bool fp32_reference {};
  bool temporal {};
  bool forward {};
  unsigned sample_count {};
  unsigned width {};
  unsigned height {};
  unsigned view_count {};
  unsigned warm_frames {};
  Format expected_format {};
  std::filesystem::path directory;
  std::filesystem::path gpu_path;
  std::filesystem::path cpu_path;
  std::filesystem::path manifest_path;
  std::array<scene::SceneNode, 2> cameras;
  std::vector<std::shared_ptr<graphics::Framebuffer>> targets;
  bool require_ready {};
  bool capture_endpoint {};
  std::array<bool, 2> seen_views {};
  std::array<unsigned, 2> formats {};
  std::array<unsigned, 2> draw_counts {};
  std::array<unsigned, 2> warm_draw_counts {};
  std::array<unsigned, 2> path_phases {};
  std::array<unsigned, 2> history_reprojected {};
  std::array<unsigned, 2> history_reset {};
  std::array<SceneTextureExtractRef, 2> endpoint_hdr;
  std::array<postprocess::ExposurePass::FrameLease, 2> endpoint_exposure;
  std::vector<Sample> samples;
  double warm_seconds {};
  double sample_seconds {};
  nlohmann::json before;
  nlohmann::json after;
  nlohmann::json endpoints;
  Sample finalization;
};
} // namespace oxygen::vortex::testing::exposure
