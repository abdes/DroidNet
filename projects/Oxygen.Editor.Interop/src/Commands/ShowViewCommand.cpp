//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <Commands/ShowViewCommand.h>
#include <EditorModule/EditorCommand.h>
#include <EditorModule/EditorView.h>
#include <EditorModule/ViewManager.h>

namespace oxygen::interop::module {

  void ShowViewCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "ShowViewCommand: ViewManager null");
      return;
    }

    auto* view = view_manager_->GetView(view_id_);
    if (!view) {
      DLOG_F(WARNING, "ShowViewCommand: invalid view id {}", view_id_.get());
      return;
    }

    // Make visible and ensure registration so rendering resumes.
    view->Show();
    view_manager_->RegisterView(view_id_);
    LOG_F(INFO, "ShowViewCommand: view {} now visible and registered",
      view_id_.get());
  }

} // namespace oxygen::interop::module
