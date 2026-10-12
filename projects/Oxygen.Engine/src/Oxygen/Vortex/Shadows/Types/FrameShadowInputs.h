//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowReceiverView.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::graphics {
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct CompositionView;
struct LightingFrameBindings;

struct PreparedViewShadowInput {
  ViewId view_id { kInvalidViewId };
  std::uint64_t scene_generation { 0U };
  std::span<const ShadowCasterDependency> shadow_caster_dependencies;
  bool shadow_dependencies_available { false };
  std::uint32_t shadow_caster_draw_count { 0U };
  observer_ptr<const LightingFrameBindings> lighting_bindings { nullptr };
  observer_ptr<const PreparedSceneFrame> prepared_scene;
  observer_ptr<const ResolvedView> resolved_view;
  //! The view's published DrawFrameBindings. Shadow and contact-shadow depth
  //! read draws, transforms, materials and world-position offsets through it,
  //! never through the camera view constants.
  ShaderVisibleIndex draw_frame_slot { kInvalidShaderVisibleIndex };
  observer_ptr<const CompositionView> composition_view;
};

struct FrameShadowInputs {
  const FrameLightSelection* frame_light_set { nullptr };
  std::span<const PreparedViewShadowInput> active_views;
  //! Full CPU preparation family; active_views alone select the views to
  //! render.
  std::span<const PreparedViewShadowInput> preparation_views;
  //! The rendered view's command list. Its directional cascades then record
  //! on it, after its depth prepass, instead of on their own list.
  observer_ptr<graphics::CommandRecorder> view_recorder { nullptr };
  //! The rendered view's receivers. With `view_recorder`, its directional
  //! cascades fit to them on the GPU; without, they keep the CPU's loose
  //! cascades.
  std::optional<shadows::ShadowReceiverView> receivers;

  [[nodiscard]] auto HasFrameLightSelection() const noexcept -> bool
  {
    return frame_light_set != nullptr;
  }
};

} // namespace oxygen::vortex
