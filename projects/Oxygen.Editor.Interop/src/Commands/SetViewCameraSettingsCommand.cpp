//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <Commands/SetViewCameraSettingsCommand.h>
#include <EditorModule/ViewManager.h>

namespace oxygen::interop::module {

  void SetViewCameraSettingsCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "SetViewCameraSettingsCommand: ViewManager null");
      return;
    }

    view_manager_->SetCameraViewSettings(view_id_, field_of_view_y_radians_,
      near_plane_, far_plane_);
  }

} // namespace oxygen::interop::module
