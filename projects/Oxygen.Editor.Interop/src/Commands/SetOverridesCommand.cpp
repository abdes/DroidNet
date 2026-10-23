//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#include <stdexcept>

#include <Commands/SetOverridesCommand.h>
#include <EditorModule/SceneAssetRequests.h>

namespace oxygen::interop::module {

void SetOverridesCommand::Execute(CommandContext &context) {
  if (!context.Scene) {
    return;
  }
  const auto node = context.Scene->GetNode(node_);
  if (!node || !node->IsAlive()) {
    return;
  }
  if (!context.AssetRequests) {
    throw std::logic_error(
        "Override command requires scene asset request state");
  }
  context.AssetRequests->SetOverrides(node_, std::move(target_),
      std::move(entries_), intent_, std::move(failure_callback_),
      std::move(success_callback_));
}

} // namespace oxygen::interop::module
