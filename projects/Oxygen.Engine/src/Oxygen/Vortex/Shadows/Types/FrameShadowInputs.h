//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex {

struct CompositionView;

struct PreparedViewShadowInput {
  ViewId view_id { kInvalidViewId };
  observer_ptr<const PreparedSceneFrame> prepared_scene;
  observer_ptr<const ResolvedView> resolved_view;
  observer_ptr<const CompositionView> composition_view;
};

struct FrameShadowInputs {
  const FrameLightSelection* frame_light_set { nullptr };
  std::span<const PreparedViewShadowInput> active_views {};

  [[nodiscard]] auto HasFrameLightSelection() const noexcept -> bool
  {
    return frame_light_set != nullptr;
  }
};

} // namespace oxygen::vortex
