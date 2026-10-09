//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <EditorModule/PaneRenderPolicy.h>

namespace oxygen::interop::module {

  void PaneRenderPolicy::Invalidate(const ViewId view) {
    if (view != kInvalidViewId) {
      panes_[view].invalidated = true;
    }
  }

  auto PaneRenderPolicy::NeedsRender(
    const ViewId view, const PaneFingerprint& fingerprint) const -> bool {
    if (always_render_ || scene_changed_) {
      return true;
    }
    const auto found = panes_.find(view);
    return found == panes_.end() || found->second.invalidated
      || found->second.rendered != fingerprint;
  }

  auto PaneRenderPolicy::IsCurrent(
    const ViewId view, const PaneFingerprint& fingerprint) const -> bool {
    const auto found = panes_.find(view);
    return found != panes_.end() && found->second.rendered == fingerprint;
  }

  void PaneRenderPolicy::MarkRendered(
    const ViewId view, const PaneFingerprint& fingerprint) {
    auto& pane = panes_[view];
    pane.rendered = fingerprint;
    pane.invalidated = false;
  }

  void PaneRenderPolicy::Forget(const ViewId view) noexcept {
    panes_.erase(view);
  }

} // namespace oxygen::interop::module
