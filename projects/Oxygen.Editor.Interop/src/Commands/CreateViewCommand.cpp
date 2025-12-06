//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "Commands/CreateViewCommand.h"
#include "EditorModule/EditorCommand.h"
#include "EditorModule/ViewManager.h"

namespace oxygen::interop::module {

  void CreateViewCommand::Execute(CommandContext& context) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "CreateViewCommand: ViewManager null");
      if (cb_)
        cb_(false, ViewId{});
      return;
    }

    if (!context.Scene) {
      DLOG_F(WARNING, "CreateViewCommand: scene missing during frame start");
      if (cb_)
        cb_(false, ViewId{});
      return;
    }

    try {
      view_manager_->CreateViewNow(std::move(cfg_), std::move(cb_));
    }
    catch (const std::exception& ex) {
      LOG_F(ERROR, "CreateViewCommand failed: {}", ex.what());
      if (cb_)
        cb_(false, ViewId{});
    }
  }

} // namespace oxygen::interop::module
