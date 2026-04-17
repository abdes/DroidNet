//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

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
    observer_ptr<graphics::Framebuffer> post_target;
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

  OXGN_VRTX_API explicit PostProcessService(Renderer& renderer);
  OXGN_VRTX_API ~PostProcessService();

  PostProcessService(const PostProcessService&) = delete;
  auto operator=(const PostProcessService&) -> PostProcessService& = delete;
  PostProcessService(PostProcessService&&) = delete;
  auto operator=(PostProcessService&&) -> PostProcessService& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_VRTX_API auto SetConfig(const PostProcessConfig& config) -> void;
  [[nodiscard]] OXGN_VRTX_API auto BuildBindings(
    const Inputs& inputs) const -> PostProcessFrameBindings;
  OXGN_VRTX_API auto PublishBindings(
    ViewId view_id, const PostProcessFrameBindings& bindings) -> ShaderVisibleIndex;
  OXGN_VRTX_API auto Execute(
    ViewId view_id, RenderContext& ctx, const SceneTextures& scene_textures,
    const Inputs& inputs) -> void;

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
  struct PublishedView {
    ShaderVisibleIndex slot { kInvalidShaderVisibleIndex };
    PostProcessFrameBindings bindings {};
  };

  auto EnsurePublishResources() -> bool;

  Renderer& renderer_;
  PostProcessConfig config_ {};
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
