//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <Commands/SetViewCameraPresetCommand.h>
#include <EditorModule/ViewManager.h>

namespace oxygen::interop::module {

  void SetViewCameraPresetCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "SetViewCameraPresetCommand: ViewManager null");
      return;
    }

    view_manager_->SetCameraViewPreset(view_id_, preset_);
  }

} // namespace oxygen::interop::module
