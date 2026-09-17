//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Framebuffer;
class Texture;
class GpuBufferReadback;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;
namespace testing {
  struct RendererPublicationProbe;
}

namespace internal {
template <typename Payload> class PerViewStructuredPublisher;
} // namespace internal

namespace postprocess {
class BloomPass;
class ExposurePass;
class TonemapPass;
} // namespace postprocess

class PostProcessService {
public:
  struct Inputs {
    const graphics::Texture* scene_signal { nullptr };
    const graphics::Texture* scene_depth { nullptr };
    const graphics::Texture* scene_velocity { nullptr };
    observer_ptr<const graphics::Framebuffer> post_target;
    ShaderVisibleIndex scene_signal_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex scene_depth_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex scene_velocity_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex bloom_texture_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex eye_adaptation_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex eye_adaptation_uav { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex post_history_srv { kInvalidShaderVisibleIndex };
  };

  struct ExecutionState {
    bool published_bindings { false };
    bool tonemap_requested { false };
    bool tonemap_executed { false };
    bool wrote_visible_output { false };
    bool bloom_requested { false };
    bool bloom_executed { false };
    bool auto_exposure_requested { false };
    bool auto_exposure_executed { false };
    bool used_fixed_exposure { false };
    ViewId view_id { kInvalidViewId };
    ShaderVisibleIndex post_process_frame_slot { kInvalidShaderVisibleIndex };
    float exposure_value { 1.0F };
  };

  enum class ExposureMaskStatus { kAbsent, kPending, kReady, kFailed };

  struct ExposureSettingsState {
    scene::ResolvedExposureSettings resolved;
    std::uint64_t revision { 0U };
    std::uint64_t lifetime { 0U };
    std::optional<scene::ExposureSettingsError> last_error;
    ExposureMaskStatus mask_status { ExposureMaskStatus::kAbsent };
    content::ResourceKey requested_mask {};
    std::string mask_error;
    std::shared_ptr<const resources::TextureBinder::ReadyTexture> mask;
  };

  OXGN_VRTX_API explicit PostProcessService(
    Renderer& renderer, observer_ptr<content::IAssetLoader> asset_loader = {});
  OXGN_VRTX_API ~PostProcessService();

  PostProcessService(const PostProcessService&) = delete;
  auto operator=(const PostProcessService&) -> PostProcessService& = delete;
  PostProcessService(PostProcessService&&) = delete;
  auto operator=(PostProcessService&&) -> PostProcessService& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_VRTX_API auto SetConfig(const PostProcessConfig& config) -> void;
  //! Resolve one complete request, retaining this view's prior valid revision.
  [[nodiscard]] OXGN_VRTX_API auto ResolveViewExposureSettings(
    CompositionView::ViewStateHandle handle,
    const scene::ExposureSettings& requested,
    std::optional<float> camera_ev = {}) -> const ExposureSettingsState&;
  //! Freeze the complete accepted revision and mask for this logical
  //! view/frame.
  [[nodiscard]] OXGN_VRTX_API auto CaptureViewExposureSettings(ViewId view_id,
    CompositionView::ViewStateHandle handle,
    const scene::ExposureSettings& requested,
    std::optional<float> camera_ev = {}, bool suppress_transitions = false,
    observer_ptr<const scene::Scene> world = {})
    -> const ExposureSettingsState&;
  OXGN_VRTX_API auto CaptureSharedExposureSource(const RenderContext& ctx,
    ViewId source_view_id, CompositionView::ViewStateHandle source_handle)
    -> const postprocess::ExposurePass::Source&;
  OXGN_VRTX_API auto CaptureRegisteredExposureControls(const RenderContext& ctx)
    -> void;
  [[nodiscard]] OXGN_VRTX_API auto BuildBindings(
    const Inputs& inputs) const -> PostProcessFrameBindings;
  OXGN_VRTX_API auto PublishBindings(
    ViewId view_id, const PostProcessFrameBindings& bindings) -> ShaderVisibleIndex;
  //! Resolve before writing pre-exposed radiance; Execute consumes this binding.
  [[nodiscard]] OXGN_VRTX_API auto PrepareFrameExposure(RenderContext& ctx,
    bool use_fp32,
    postprocess::ExposurePass::StateLease qualified_candidate = {})
    -> postprocess::ExposurePass::FrameLease;
  struct PreparedExposure {
    const PostProcessService* owner;
    postprocess::ExposurePass::Result exposure;
    PostProcessConfig config;
    ViewId view_id;
    CompositionView::ViewStateHandle handle;
    std::uint64_t lifetime;
    frame::SequenceNumber sequence;
  };
  //! Solve from the accumulated scene signal before checked color resolution.
  //! The returned record pins the result/config for this view and logical
  //! frame.
  [[nodiscard]] OXGN_VRTX_API auto PrepareSceneExposure(
    ViewId view_id, RenderContext& ctx, const Inputs& inputs)
    -> std::optional<PreparedExposure>;
  OXGN_VRTX_API auto Execute(ViewId view_id, RenderContext& ctx,
    const SceneTextures& scene_textures, const Inputs& inputs,
    const PreparedExposure* prepared_exposure = nullptr) -> void;
  OXGN_VRTX_API auto RemoveViewState(ViewId view_id,
    CompositionView::ViewStateHandle view_state_handle
    = CompositionView::kInvalidViewStateHandle) -> void;
  OXGN_VRTX_API auto PreserveRemovedExposureSource(
    std::shared_ptr<const ExposureSourceLoss> loss,
    CompositionView::ViewStateHandle only_consumer
    = CompositionView::kInvalidViewStateHandle) -> void;

  [[nodiscard]] OXGN_VRTX_API auto InspectBindings(ViewId view_id) const
    -> const PostProcessFrameBindings*;
  [[nodiscard]] OXGN_VRTX_API auto ResolveBindingSlot(ViewId view_id) const
    -> ShaderVisibleIndex;
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetConfig() const noexcept
    -> const PostProcessConfig&
  {
    return config_;
  }
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastExecutionState() const noexcept
    -> const ExecutionState&
  {
    return last_execution_state_;
  }

private:
  friend struct testing::RendererPublicationProbe;
  auto CaptureConfiguredExposure(ViewId view_id, RenderContext& ctx)
    -> const ExposureSettingsState&;
  struct CapturedExposureSettings {
    CompositionView::ViewStateHandle handle;
    ExposureSettingsState settings;
  };
  using ExposureCaptureKey
    = std::pair<ViewId, CompositionView::ViewStateHandle>;
  std::map<ExposureCaptureKey, CapturedExposureSettings>
    captured_exposure_settings_;
  std::unordered_map<ViewId, postprocess::ExposurePass::Source>
    captured_exposure_sources_;
  std::optional<frame::SequenceNumber> captured_control_frame_;
  auto BuildBindings(const Inputs& inputs,
    const PostProcessConfig& config) const -> PostProcessFrameBindings;
  struct PublishedView {
    ShaderVisibleIndex slot { kInvalidShaderVisibleIndex };
    PostProcessFrameBindings bindings {};
  };

  struct PendingExposureStatus {
    postprocess::ExposurePass::StateLease state;
    std::shared_ptr<graphics::GpuBufferReadback> readback;
    ExposureTransitionToken token;
    std::uint64_t frame_sequence;
    std::uint64_t settings_revision;
  };
  std::unordered_map<CompositionView::ViewStateHandle,
    std::deque<PendingExposureStatus>>
    pending_exposure_status_;
  std::unordered_map<CompositionView::ViewStateHandle, PendingExposureStatus>
    deferred_exposure_status_;
  auto IsExposureStatusNeeded(const PendingExposureStatus& job) const -> bool;
  auto DeferExposureStatus(PendingExposureStatus job) -> void;
  auto TryEnqueueExposureStatus(PendingExposureStatus job) -> bool;
  auto PollExposureStatus() -> void;
  OXGN_VRTX_API auto EnqueueExposureStatus(const ExposureTransitionToken& token,
    postprocess::ExposurePass::StateLease state, const RenderContext& ctx,
    std::uint64_t settings_revision) -> void;

  auto EnsurePublishResources() -> bool;
  auto EnsureMaskBinder() -> resources::TextureBinder*;

  Renderer& renderer_;
  observer_ptr<content::IAssetLoader> asset_loader_;
  std::unique_ptr<resources::TextureBinder> mask_binder_;
  std::array<
    std::vector<std::shared_ptr<const resources::TextureBinder::ReadyTexture>>,
    frame::kFramesInFlight.get()>
    frame_masks_;

  PostProcessConfig config_ {};
  std::unordered_map<CompositionView::ViewStateHandle, ExposureSettingsState>
    exposure_settings_;
  ExposureSettingsState transient_exposure_settings_ {};
  frame::SequenceNumber current_sequence_ { 0U };
  frame::Slot current_slot_ { frame::kInvalidSlot };
  std::unique_ptr<internal::PerViewStructuredPublisher<PostProcessFrameBindings>>
    bindings_publisher_ {};
  std::unordered_map<ViewId, PublishedView> published_views_ {};
  ExecutionState last_execution_state_ {};
  std::unique_ptr<postprocess::ExposurePass> exposure_pass_ {};
  std::unique_ptr<postprocess::BloomPass> bloom_pass_ {};
  std::unique_ptr<postprocess::TonemapPass> tonemap_pass_ {};
};

} // namespace oxygen::vortex
