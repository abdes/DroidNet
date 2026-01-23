//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen {
class ResolvedView;
} // namespace oxygen::core

namespace oxygen::engine::sceneprep {

//! Shared context passed to ScenePrep algorithms.
/*!
 Provides read-only frame, view and scene information along with a mutable
 `RenderContext` for resource operations. The context must outlive the
 ScenePrep invocation that receives it.
 */
class ScenePrepContext {
public:
  //! Construct a ScenePrepContext that borrows the provided references.
  //! The view argument is optional; a null observer_ptr indicates Frame-phase
  //! invocation where no specific view is available.
  ScenePrepContext(frame::SequenceNumber fseq,
    oxygen::observer_ptr<const ResolvedView> v, const scene::Scene& s) noexcept
    : frame_seq_number { fseq }
    , view_ { v }
    , scene_ { std::ref(s) }
  {
  }

  OXYGEN_DEFAULT_COPYABLE(ScenePrepContext)
  OXYGEN_DEFAULT_MOVABLE(ScenePrepContext)

  ~ScenePrepContext() noexcept = default;

  [[nodiscard]] auto GetFrameSequenceNumber() const noexcept
  {
    return frame_seq_number;
  }
  [[nodiscard]] auto HasView() const noexcept
  {
    return static_cast<bool>(view_);
  }

  [[nodiscard]] auto GetView() const noexcept -> const ResolvedView&
  {
    return *view_;
  }
  [[nodiscard]] auto& GetScene() const noexcept { return scene_.get(); }
  // NOTE: RenderContext removed; reintroduce if extractors require GPU ops.

private:
  //! Current frame identifier for temporal coherency optimizations.
  frame::SequenceNumber frame_seq_number;

  //! View containing camera matrices and frustum for the current frame.
  //! May be null (observer) when operating in Frame-phase.
  oxygen::observer_ptr<const ResolvedView> view_;

  //! Scene graph being processed.
  std::reference_wrapper<const scene::Scene> scene_;
};

} // namespace oxygen::engine::sceneprep
