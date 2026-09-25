//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <chrono>
#include <filesystem>

#include <nlohmann/json.hpp>

#include <Oxygen/Vortex/Lighting/Types/LightGridResources.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Support/CpuTimingCapture.h>
#include <Oxygen/Vortex/Test/Support/LightingWorkloadScene.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>

namespace oxygen::vortex::testing {

//! Opt-in native rendering workload. Readbacks and serialization of images and
//! resource inventories occur outside the timed frame window.
class ManyLightBaseline : public exposure::ExposureLightingGpuTest {
protected:
  auto SetUp() -> void override;
  auto TearDown() -> void override;
  auto ConfigureRenderer(RendererConfig& config) const -> void override;
  auto BackendConfigJson() const -> std::string override;
  auto AdditionalCapabilities() const -> CapabilitySet override;
  auto RunBaseline() -> void;

private:
  using Clock = std::chrono::steady_clock;
  struct Sample {
    unsigned sequence {};
    double wall_ms {};
    double frame_start_ms {};
    double scene_update_ms {};
    double submission_ms {};
    unsigned shadow_writers {};
    unsigned shadow_map_uses {};
    unsigned shadow_backing_uses {};
  };
  struct CapturedView {
    std::shared_ptr<const graphics::Texture> hdr;
    postprocess::ExposurePass::FrameLease exposure;
    LightGridResources grid;
    LightingFrameBindings bindings;
    unsigned point_shadows {};
    unsigned spot_shadows {};
    unsigned quality_omissions {};
    unsigned draws {};
  };
  auto SetupCase() -> void;
  auto RenderFrame(unsigned motion_frame, bool begin_recording = false)
    -> Sample;
  auto Capture(unsigned motion_frame) -> void;
  auto SnapshotResources() -> nlohmann::json;
  auto WriteResults() -> void;
  static auto Milliseconds(Clock::duration duration) -> double;

  nlohmann::json request_;
  LightingWorkloadOptions options_;
  LightingWorkload workload_;
  LightingWorkloadScene scene_owner_;
  std::vector<LightingWorkload> motion_cycle_;
  std::filesystem::path directory_;
  std::vector<std::shared_ptr<graphics::Framebuffer>> targets_;
  std::vector<CapturedView> captured_;
  std::vector<Sample> samples_;
  std::unique_ptr<CpuTimingCapture> cpu_;
  nlohmann::json snapshots_ = nlohmann::json::array();
  nlohmann::json images_ = nlohmann::json::array();
  bool forward_ {};
  bool reference_ {};
  bool measure_ {};
  bool capture_ {};
  bool recording_ {};
  unsigned frame_shadow_writers_ { 0 };
  unsigned frame_shadow_map_uses_ { 0 };
  unsigned frame_shadow_backing_uses_ { 0 };
  unsigned warmup_frames_ { 120U };
  unsigned sample_frames_ { 240U };
};
} // namespace oxygen::vortex::testing
