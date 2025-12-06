//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"
#include "Commands/HideViewCommand.h"
#include "EditorModule/ViewManager.h"
#include "EditorModule/EditorView.h"

namespace oxygen::interop::module {

void HideViewCommand::Execute(CommandContext& /*context*/) {
  DLOG_SCOPE_F(4, "HideViewCommand::Execute");

  if (manager_ == nullptr) {
    DLOG_F(WARNING, "HideViewCommand: ViewManager null");
    return;
  }

  auto *view = manager_->GetView(view_id_);
  if (!view) {
    DLOG_F(WARNING, "HideViewCommand: invalid view id {}", view_id_.get());
    return;
  }

  // Mark hidden and unregister from FrameContext so it stops rendering.
  view->Hide();
  manager_->UnregisterView(view_id_);
  LOG_F(INFO, "HideViewCommand: view {} hidden and unregistered", view_id_.get());
}

} // namespace oxygen::interop::module
