//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <stdexcept>

#include <Commands/SetMaterialOverrideCommand.h>
#include <EditorModule/SceneAssetRequests.h>

namespace oxygen::interop::module {

void SetMaterialOverrideCommand::Execute(CommandContext &context) {
  if (!context.Scene) {
    return;
  }
  const auto node = context.Scene->GetNode(node_);
  if (!node || !node->IsAlive()) {
    return;
  }
  if (!context.AssetRequests) {
    throw std::logic_error(
        "Material command requires scene asset request state");
  }
  context.AssetRequests->SetMaterial(
      node_, slot_index_, material_uri_, std::move(failure_callback_));
}

} // namespace oxygen::interop::module
