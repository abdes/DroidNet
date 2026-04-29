//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <Commands/SetViewCameraMovementSpeedCommand.h>
#include <EditorModule/ViewManager.h>

namespace oxygen::interop::module {

  void SetViewCameraMovementSpeedCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "SetViewCameraMovementSpeedCommand: ViewManager null");
      return;
    }

    view_manager_->SetCameraMovementSpeed(
      view_id_, speed_units_per_second_);
  }

} // namespace oxygen::interop::module
