//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <optional>
#include <unordered_map>

#include <glm/mat4x4.hpp>

#include <Oxygen/Core/Types/View.h>

namespace oxygen::interop::module {

  //! What a pane's camera shows: its resolved matrices and its extent.
  struct PaneFingerprint {
    glm::mat4 view { 1.0F };
    glm::mat4 projection { 1.0F };
    float width { 0.0F };
    float height { 0.0F };

    auto operator==(const PaneFingerprint&) const -> bool = default;
  };

  //! Decides which viewport panes render this frame.
  /*!
   A pane renders when something it shows may have changed: a change visible
   in every pane (scene edit, selection, newly resident content), a change to
   that pane alone (its settings, input routed to it, a pending pick), or a
   different camera image than the one it last rendered. Otherwise it keeps
   its last image. With always-render on, every pane renders each frame.

   Engine-thread only.
  */
  class PaneRenderPolicy {
  public:
    //! Starts a frame; `scene_changed` covers changes visible in every pane.
    void BeginFrame(bool scene_changed) noexcept {
      scene_changed_ = scene_changed;
    }

    //! Marks one pane as needing a render this frame; an invalid id is ignored.
    void Invalidate(ViewId view);

    //! Whether the pane must render this frame to show what it should.
    [[nodiscard]] auto NeedsRender(
      ViewId view, const PaneFingerprint& fingerprint) const -> bool;

    //! Whether the pane last rendered exactly this camera image.
    [[nodiscard]] auto IsCurrent(
      ViewId view, const PaneFingerprint& fingerprint) const -> bool;

    //! Records that the pane renders `fingerprint` this frame.
    void MarkRendered(ViewId view, const PaneFingerprint& fingerprint);

    //! Drops a pane's history so its next decision renders it.
    void Forget(ViewId view) noexcept;

    void SetAlwaysRender(bool always_render) noexcept {
      always_render_ = always_render;
    }
    [[nodiscard]] auto IsAlwaysRender() const noexcept -> bool {
      return always_render_;
    }

  private:
    struct PaneState {
      std::optional<PaneFingerprint> rendered;
      bool invalidated { false };
    };

    std::unordered_map<ViewId, PaneState> panes_;
    bool scene_changed_ { false };
    bool always_render_ { false };
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
