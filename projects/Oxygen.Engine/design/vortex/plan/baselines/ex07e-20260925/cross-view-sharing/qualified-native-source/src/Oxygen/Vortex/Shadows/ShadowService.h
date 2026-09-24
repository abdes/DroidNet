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
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterDependencies.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowContentLease.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowSharingDiagnostics.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class Renderer;

namespace internal {
  template <typename Payload> class PerViewStructuredPublisher;
} // namespace internal

namespace upload {
  class TransientStructuredBuffer;
}

namespace shadows {
  class CascadeShadowPass;
  class ContactShadowCasterDepthPass;
} // namespace shadows

class ShadowService {
public:
  struct RenderState {
    frame::SequenceNumber frame_sequence { 0U };
    frame::Slot frame_slot { frame::kInvalidSlot };
    std::uint32_t published_view_count { 0U };
    std::uint32_t directional_view_count { 0U };
    std::uint32_t spot_view_count { 0U };
    std::uint32_t point_view_count { 0U };
    std::uint32_t rendered_cascade_count { 0U };
    std::uint32_t rendered_spot_shadow_count { 0U };
    std::uint32_t rendered_point_shadow_count { 0U };
    std::uint32_t rendered_draw_count { 0U };
    std::uint32_t shadow_caster_draw_count { 0U };
    std::uint64_t selection_epoch { 0U };
    std::uint32_t attached_map_uses { 0 };
    std::uint32_t attached_backing_uses { 0 };
  };

  OXGN_VRTX_API explicit ShadowService(Renderer& renderer);
  OXGN_VRTX_API ~ShadowService();
  [[nodiscard]] OXGN_VRTX_API auto InspectLocalSharing() const
    -> ShadowSharingDiagnostics;

  ShadowService(const ShadowService&) = delete;
  auto operator=(const ShadowService&) -> ShadowService& = delete;
  ShadowService(ShadowService&&) = delete;
  auto operator=(ShadowService&&) -> ShadowService& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_VRTX_API auto CloseFramePublications() noexcept -> void;
  OXGN_VRTX_API auto PrepareLocalRequests(const FrameShadowInputs& inputs)
    -> void;
  OXGN_VRTX_API auto RenderShadowDepths(const FrameShadowInputs& inputs)
    -> void;

  [[nodiscard]] OXGN_VRTX_API auto InspectReadSet(ViewId view) const
    -> std::shared_ptr<const ShadowFrameReadSet>;
  [[nodiscard]] OXGN_VRTX_API auto RetainLocalContent(
    ViewId view, scene::NodeHandle light) const -> ShadowContentLease;
  OXGN_VRTX_API auto AttachLocalReads(ViewId view, frame::SequenceNumber frame,
    std::uint64_t preparation_revision, graphics::CommandRecorder& recorder)
    -> void;
  [[nodiscard]] OXGN_VRTX_API auto InspectPreparationFailure(
    ViewId view_id) const -> const LightingPreparationFailure*;
  [[nodiscard]] OXGN_VRTX_API auto InspectShadowData(ViewId view_id) const
    -> const ShadowFrameData*;
  [[nodiscard]] OXGN_VRTX_API auto InspectDirectionalShadowSurfaces(
    ViewId view_id) const
    -> std::span<const std::shared_ptr<graphics::Texture>>;
  [[nodiscard]] OXGN_VRTX_API auto InspectSpotShadowSurfaces(
    ViewId view_id) const
    -> std::span<const std::shared_ptr<graphics::Texture>>;
  [[nodiscard]] OXGN_VRTX_API auto InspectPointShadowSurfaces(
    ViewId view_id) const
    -> std::span<const std::shared_ptr<graphics::Texture>>;
  [[nodiscard]] OXGN_VRTX_API auto InspectContactShadowSurface(
    ViewId view_id) const -> std::shared_ptr<const graphics::Texture>;
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
    ShadowFrameData data {};
    std::vector<std::shared_ptr<graphics::Texture>> directional_surfaces;
    std::vector<std::shared_ptr<graphics::Texture>> spot_surfaces;
    std::vector<std::shared_ptr<graphics::Texture>> point_surfaces;
    std::shared_ptr<graphics::Texture> contact_surface;
    std::vector<std::shared_ptr<shadows::internal::ShadowMapOwner>> local_maps;
    std::shared_ptr<ShadowFrameReadSet> read_set;
    std::uint64_t scene_generation { 0 };
    std::uint64_t preparation_revision { 0 };
    std::uint64_t selection_epoch { 0 };
  };

  auto EnsurePublishResources() -> bool;
  auto PublishShadowBindings(ViewId view_id, ShadowFrameData& data)
    -> ShaderVisibleIndex;

  struct PreparedCasters {
    frame::SequenceNumber last_seen { 0 };
    std::uint64_t revision { 0 };
    std::vector<ShadowCasterDependency> dependencies;
    std::uint32_t draw_count { 0 };
    bool available { false };
  };
  std::vector<PreparedViewShadowInput> family_views_;
  shadows::internal::ShadowCasterDependencies caster_records_;
  std::unordered_map<ViewId, PreparedCasters> prepared_casters_;
  Renderer& renderer_;
  frame::SequenceNumber current_sequence_ { 0U };
  frame::Slot current_slot_ { frame::kInvalidSlot };
  RenderState last_render_state_ {};
  std::unique_ptr<internal::PerViewStructuredPublisher<ShadowFrameBindings>>
    bindings_publisher_;
  std::unique_ptr<upload::TransientStructuredBuffer> directional_record_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer> cascade_record_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer> projected_record_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer> cube_record_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer>
    directional_reference_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer> local_reference_buffer_;
  std::unordered_map<ViewId, PublishedView> published_views_;
  std::unordered_map<ViewId, LightingPreparationFailure> failed_views_;
  std::unique_ptr<shadows::CascadeShadowPass> cascade_shadow_pass_;
  std::unique_ptr<shadows::ContactShadowCasterDepthPass> contact_depth_pass_;
};

} // namespace oxygen::vortex
