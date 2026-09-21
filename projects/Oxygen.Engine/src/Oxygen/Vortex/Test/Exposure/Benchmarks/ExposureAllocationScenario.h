//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Vortex/PostProcess/Passes/TonemapPass.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>

namespace oxygen::vortex::testing::exposure {

// Owns exactly one allocation-accounting scenario and its delayed consumers.
// The fixture owns the renderer; this class owns only the accounting resources.
class ExposureAllocationScenario final {
public:
  ExposureAllocationScenario(ExposureLightingGpuTest& fixture, bool temporal);
  ~ExposureAllocationScenario();
  ExposureAllocationScenario(const ExposureAllocationScenario&) = delete;
  auto operator=(const ExposureAllocationScenario&)
    -> ExposureAllocationScenario& = delete;
  ExposureAllocationScenario(ExposureAllocationScenario&&) = delete;
  auto operator=(ExposureAllocationScenario&&)
    -> ExposureAllocationScenario& = delete;
  auto Run() -> void;

private:
  struct DepthLocation {
    unsigned view_index {};
    unsigned alias {};
  };
  struct FrameRecipe {
    unsigned view_count {};
    unsigned layout {};
    float ev {};
  };
  struct DepthReadback {
    std::shared_ptr<graphics::Buffer> buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    UINT64 row_bytes {
      0U,
    };
    UINT64 alias_stride {
      0U,
    };
  };
  struct ViewRecord {
    ViewId id {};
    SceneTextureExtractRef color;
    std::array<SceneTextureExtractRef, 2> depths;
    unsigned draws {};
  };

  // Non-owning lifetime observations plus the deliberately queued depth
  // command. Retained texture ownership remains in retained_ until
  // ReleaseAndSubmit.
  struct CycleObservation {
    std::array<std::uint32_t, 2> reference_depth {};
    std::array<ExposureStateData, 2> saved_states {};
    std::array<FrameExposureData, 2> saved_domains {};
    std::array<std::uint64_t, 2> saved_lifetimes {};
    std::array<std::weak_ptr<const graphics::Texture>, 2> wrapper_lifetimes;
    std::array<std::weak_ptr<const graphics::Texture>, 2> resource_lifetimes;
    std::array<std::weak_ptr<const graphics::Texture>, 2>
      depth_wrapper_lifetimes;
    std::array<std::weak_ptr<const graphics::Texture>, 2>
      depth_resource_lifetimes;
    std::shared_ptr<const graphics::CommandList> depth_recording;
  };

  static auto ReadEnvironment(const char* name, std::string_view fallback)
    -> std::string;
  auto SetUp() -> void;
  auto CleanUp() noexcept -> void;
  auto RunLifecycle() -> void;
  auto QualifyAndRetain(
    const std::string& prefix, CycleObservation& observation) -> void;
  auto ResizeAndCapture(
    const std::string& prefix, CycleObservation& observation) -> void;
  auto QueueConsumers(const std::string& prefix, CycleObservation& observation)
    -> void;
  auto RemoveAndReadd(const std::string& prefix, CycleObservation& observation)
    -> void;
  auto ReleaseAndSubmit(
    const std::string& prefix, CycleObservation& observation) -> void;
  auto VerifyConsumers(unsigned cycle, CycleObservation& observation) -> void;
  auto RetireCycle(unsigned cycle, const std::string& prefix,
    CycleObservation& observation) -> void;

  auto WriteReport() -> void;
  auto Snapshot(const std::string& phase) -> nlohmann::json;
  auto RenderFrame(FrameRecipe recipe, const std::string& phase) -> void;
  auto RemoveViews() -> void;
  auto Srv(const graphics::Texture& texture) -> ShaderVisibleIndex;
  static auto Population(const nlohmann::json& snapshot)
    -> std::vector<std::string>;
  auto EnsureDepthReadback(unsigned index, const graphics::Texture& depth)
    -> void;
  auto CopyDepth(graphics::CommandRecorder& recorder,
    const graphics::Texture& depth, DepthLocation location) -> void;
  auto DepthSample(DepthLocation location) -> std::uint32_t;

  template <typename Action> auto WithoutDiagnostics(Action&& action)
  {
    const bool tracked = std::exchange(backend_->track_resources, false);
    const bool accounted
      = std::exchange(backend_->account_texture_allocations, false);
    auto restore = ScopeGuard([&] noexcept -> void {
      backend_->track_resources = tracked;
      backend_->account_texture_allocations = accounted;
    });
    return std::forward<Action>(action)();
  }

  ExposureLightingGpuTest& fixture_;
  bool temporal_;
  std::uint32_t width_ {
    0U,
  };
  std::uint32_t height_ {
    0U,
  };
  std::string precision_;
  bool fp32_reference_ {
    false,
  };
  ExposureFailureGraphics* backend_ {
    nullptr,
  };
  std::array<std::shared_ptr<graphics::Texture>, 2> outputs_;
  std::array<std::shared_ptr<graphics::Framebuffer>, 2> targets_;
  std::array<std::shared_ptr<graphics::Texture>, 2> consumer_outputs_;
  std::array<std::shared_ptr<graphics::Framebuffer>, 2> consumer_targets_;
  std::array<DepthReadback, 2> depth_readbacks_;
  std::optional<postprocess::TonemapPass> consumer_;
  std::vector<graphics::CommandRecording> pending_consumers_;
  std::vector<std::shared_ptr<const graphics::CommandList>>
    pending_tonemap_lists_;
  bool cleanup_armed_ {
    false,
  };
  std::unordered_map<CompositionView::ViewStateHandle, ViewRecord> current_;
  std::array<ViewRecord, 2> retained_;
  nlohmann::json phases_ = nlohmann::json::array();
  nlohmann::json frames_ = nlohmann::json::array();
  std::vector<std::string> retired_population_;
  nlohmann::json consumer_results_ = nlohmann::json::array();
  nlohmann::json depth_results_ = nlohmann::json::array();
};

} // namespace oxygen::vortex::testing::exposure
