//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen {
class View;
}

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
  ScenePrepContext(
    frame::SequenceNumber fseq, const View& v, const scene::Scene& s) noexcept
    : frame_seq_number { fseq }
    , view_ { std::ref(v) }
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
  [[nodiscard]] auto& GetView() const noexcept { return view_.get(); }
  [[nodiscard]] auto& GetScene() const noexcept { return scene_.get(); }
  // NOTE: RenderContext removed; reintroduce if extractors require GPU ops.

private:
  //! Current frame identifier for temporal coherency optimizations.
  frame::SequenceNumber frame_seq_number;

  //! View containing camera matrices and frustum for the current frame.
  std::reference_wrapper<const View> view_;

  //! Scene graph being processed.
  std::reference_wrapper<const scene::Scene> scene_;
};

} // namespace oxygen::engine::sceneprep
