//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "Commands/DestroyViewCommand.h"
#include "EditorModule/EditorCommand.h"
#include "EditorModule/ViewManager.h"

namespace oxygen::interop::module {

  void DestroyViewCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "DestroyViewCommand: ViewManager null");
      return;
    }

    view_manager_->DestroyView(view_id_);
    LOG_F(INFO, "DestroyViewCommand: view {} destroyed", view_id_.get());
  }

} // namespace oxygen::interop::module
