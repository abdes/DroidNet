//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <unordered_map>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::internal {

class PreviousViewHistoryCache {
public:
  struct CurrentState {
    glm::mat4 view_matrix { 1.0F };
    glm::mat4 projection_matrix { 1.0F };
    glm::mat4 stable_projection_matrix { 1.0F };
    glm::mat4 inverse_view_projection_matrix { 1.0F };
    glm::vec2 pixel_jitter { 0.0F, 0.0F };
    ViewPort viewport {};
  };

  struct Snapshot {
    CurrentState current {};
    CurrentState previous {};
    bool previous_valid { false };
  };

  OXGN_VRTX_API void BeginFrame(
    std::uint64_t frame_sequence, observer_ptr<const scene::Scene> scene);
  OXGN_VRTX_API auto TouchCurrent(
    CompositionView::ViewStateHandle view_state_handle,
    const CurrentState& current) -> Snapshot;
  [[nodiscard]] OXGN_VRTX_API auto TouchStateless(
    const CurrentState& current) const -> Snapshot;
  OXGN_VRTX_API void EndFrame();

private:
  struct Entry {
    CurrentState current {};
    CurrentState previous {};
    std::uint64_t last_seen_frame { 0U };
    bool previous_valid { false };
  };

  std::uint64_t current_frame_ { 0U };
  const scene::Scene* current_scene_ { nullptr };
  std::unordered_map<CompositionView::ViewStateHandle, Entry> entries_ {};
};

} // namespace oxygen::vortex::internal
