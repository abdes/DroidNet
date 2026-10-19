//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <Commands/SetGeometryCommand.h>
#include <EditorModule/EditorCommand.h>
#include <EditorModule/SceneAssetRequests.h>

#include <Oxygen/Data/GeometryAsset.h>

namespace oxygen::interop::module {

void SetGeometryCommand::Execute(CommandContext &context) {
  if (!context.Scene) {
    return;
  }

  const auto scene_node_opt = context.Scene->GetNode(node_);
  if (!scene_node_opt || !scene_node_opt->IsAlive())
    return;

  if (!context.AssetRequests) {
    throw std::logic_error(
        "Geometry command requires scene asset request state");
  }
  auto complete = context.AssetRequests->BeginGeometry(
      node_, assetUri_, std::move(failure_callback_), std::move(success_callback_));
  try {
    context.AssetRequests->LoadGeometry(assetUri_, complete);
  } catch (const std::exception &ex) {
    complete({}, ex.what());
  } catch (...) {
    complete({}, "unknown exception creating geometry");
  }
}

} // namespace oxygen::interop::module
