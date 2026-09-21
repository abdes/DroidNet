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

#include <Oxygen/Vortex/Benchmarks/ExposureBenchmarkFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
namespace oxygen::vortex::testing::exposure {

class ExposureCpuTiming;

// Owns one benchmark execution; timed rendering is separate from evidence I/O.
class ExposureBaselineScenario final {
public:
  ExposureBaselineScenario(ExposureProfilingOverheadTest& fixture,
    ExposureProfilingOverheadTest::BaselineRecipe kind);
  ~ExposureBaselineScenario();
  ExposureBaselineScenario(const ExposureBaselineScenario&) = delete;
  auto operator=(const ExposureBaselineScenario&)
    -> ExposureBaselineScenario& = delete;
  ExposureBaselineScenario(ExposureBaselineScenario&&) = delete;
  auto operator=(ExposureBaselineScenario&&)
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
  struct Window {
    std::string name;
    std::filesystem::path gpu;
    std::vector<Sample> samples;
    double seconds {};
  };
  struct EventObservation {
    unsigned event_frame {};
    std::array<std::uint64_t, 2> lifetimes {};
    std::array<std::uint64_t, 2> requested {};
    std::array<std::uint64_t, 2> applied {};
    std::array<unsigned, 2> phases {};
    std::array<bool, 2> active {};
    unsigned held_status_jobs {};
  };
  struct EventCheckpoint {
    unsigned event_frame {};
    std::array<postprocess::ExposurePass::FrameLease, 2> exposure;
    std::array<unsigned, 2> formats {};
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
  auto FinalizeRecording(unsigned path_frame) -> void;
  auto MeasureEventWindows() -> void;
  auto ApplyEvent(unsigned event_frame) -> void;
  auto ObserveEvent(unsigned event_frame) -> void;
  auto SaveAcceptanceWindows() -> void;
  auto SaveWindow(const Window& window) -> void;

  static constexpr auto quality_commands = std::array {
    "vtx.sky_atmosphere.aerial_perspective_lut.width 64",
    "vtx.sky_atmosphere.aerial_perspective_lut.depth_resolution 32",
    "vtx.sky_atmosphere.aerial_perspective_lut.depth_km 96.0",
    "vtx.sky_atmosphere.aerial_perspective_lut.sample_count_max_per_slice 2.0",
    "vtx.volumetric_fog.history_miss_supersample_count 4",
    "vtx.volumetric_fog.directional_shadows true",
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
  bool fp32_only {};
  bool warmup_only {};
  bool automatic_sample_count {};
  bool acceptance {};
  bool measure_cpu_owners {};
  bool measure_cpu_details {};
  bool recording_cpu_owners {};
  std::unique_ptr<ExposureCpuTiming> cpu_timing;
  bool event_cycle {};
  bool capture_event {};
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
  std::filesystem::path recording_path;
  unsigned recording_frames {};
  std::array<scene::SceneNode, 2> cameras;
  scene::SceneNode sun;
  std::array<bool, 2> enabled_views {
    true,
    true,
  };
  std::array<unsigned, 2> target_indices {
    0U,
    1U,
  };
  std::array<scene::ExposureSettings, 2> view_settings;
  ViewId secondary_source {
    kInvalidViewId,
  };
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
  Window startup;
  Window matched;
  Window events;
  std::vector<EventObservation> observations;
  std::vector<EventCheckpoint> checkpoints;
  EventCheckpoint current_checkpoint;
  vortex::testing::RendererPublicationProbe::ExposureStatusDelivery
    held_statuses;
  nlohmann::json event_operations = nlohmann::json::array();
  nlohmann::json acceptance_windows = nlohmann::json::object();
};
} // namespace oxygen::vortex::testing::exposure
