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
#include <Oxygen/Vortex/Shadows/Types/DirectionalShadowFrameData.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class Renderer;

namespace internal {
template <typename Payload> class PerViewStructuredPublisher;
} // namespace internal

namespace shadows {
class CascadeShadowPass;
} // namespace shadows

class ShadowService {
public:
  struct RenderState {
    frame::SequenceNumber frame_sequence { 0U };
    frame::Slot frame_slot { frame::kInvalidSlot };
    std::uint32_t published_view_count { 0U };
    std::uint32_t directional_view_count { 0U };
    std::uint32_t rendered_cascade_count { 0U };
    std::uint32_t rendered_draw_count { 0U };
    std::uint32_t shadow_caster_draw_count { 0U };
    std::uint64_t selection_epoch { 0U };
  };

  OXGN_VRTX_API explicit ShadowService(Renderer& renderer);
  OXGN_VRTX_API ~ShadowService();

  ShadowService(const ShadowService&) = delete;
  auto operator=(const ShadowService&) -> ShadowService& = delete;
  ShadowService(ShadowService&&) = delete;
  auto operator=(ShadowService&&) -> ShadowService& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_VRTX_API auto RenderShadowDepths(const FrameShadowInputs& inputs) -> void;

  [[nodiscard]] OXGN_VRTX_API auto InspectShadowData(ViewId view_id) const
    -> const DirectionalShadowFrameData*;
  [[nodiscard]] OXGN_VRTX_API auto ResolveShadowFrameSlot(ViewId view_id) const
    -> ShaderVisibleIndex;
  [[nodiscard]] OXGN_VRTX_NDAPI auto HasVsm() const -> bool { return false; }
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastRenderState() const noexcept
    -> const RenderState&
  {
    return last_render_state_;
  }

private:
  struct PublishedView {
    ShaderVisibleIndex slot { kInvalidShaderVisibleIndex };
    DirectionalShadowFrameData data {};
  };

  auto EnsurePublishResources() -> bool;
  auto PublishShadowBindings(
    ViewId view_id, const ShadowFrameBindings& bindings) -> ShaderVisibleIndex;

  Renderer& renderer_;
  frame::SequenceNumber current_sequence_ { 0U };
  frame::Slot current_slot_ { frame::kInvalidSlot };
  RenderState last_render_state_ {};
  std::unique_ptr<internal::PerViewStructuredPublisher<ShadowFrameBindings>>
    bindings_publisher_ {};
  std::unordered_map<ViewId, PublishedView> published_views_ {};
  std::unique_ptr<shadows::CascadeShadowPass> cascade_shadow_pass_ {};
};

} // namespace oxygen::vortex
