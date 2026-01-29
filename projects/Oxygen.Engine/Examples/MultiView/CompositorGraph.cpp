//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Base/Logging.h>

#include "MultiView/CompositorGraph.h"
#include "MultiView/DemoView.h"

namespace oxygen::examples::multiview {

auto CompositorGraph::Execute(const Inputs& inputs) const -> co::Co<>
{
  CHECK_F(!inputs.views.empty(), "CompositorGraph requires at least one view");

  const auto gui_view_count = std::ranges::count_if(inputs.views,
    [](const DemoView* view) { return view && view->IsGuiEnabled(); });
  CHECK_F(
    gui_view_count <= 1, "CompositorGraph allows only one GUI-enabled view");

  LOG_F(INFO, "[CompositorGraph] Executing with {} views (gui_views={})",
    inputs.views.size(), gui_view_count);

  for (auto* view : inputs.views) {
    CHECK_NOTNULL_F(view, "CompositorGraph received a null view");
    view->Composite(inputs.recorder, inputs.backbuffer);
  }

  for (auto* view : inputs.views) {
    CHECK_NOTNULL_F(view, "CompositorGraph received a null view");
    co_await view->RenderGuiAfterComposite(
      inputs.recorder, inputs.backbuffer_framebuffer);
  }

  co_return;
}

} // namespace oxygen::examples::multiview
