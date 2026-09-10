//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <stdexcept>

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/SceneAssetRequests.h>

namespace oxygen::interop::module {

  class DetachGeometryCommand : public EditorCommand {
  public:
    DetachGeometryCommand(oxygen::scene::NodeHandle node);

    void Execute(CommandContext& context) override;

  private:
    oxygen::scene::NodeHandle node_;
  };

  inline DetachGeometryCommand::DetachGeometryCommand(
    oxygen::scene::NodeHandle node)
    : EditorCommand(oxygen::core::PhaseId::kSceneMutation), node_(node) {
  }

  inline void DetachGeometryCommand::Execute(CommandContext& context) {
    if (!context.Scene)
      return;

    auto sceneNode = context.Scene->GetNode(node_);
    if (!sceneNode || !sceneNode->IsAlive())
      return;

    if (!context.AssetRequests) {
      throw std::logic_error("Detach command requires scene asset request state");
    }
    context.AssetRequests->Detach(node_);
    auto r = sceneNode->GetRenderable();
    static_cast<void>(r.Detach());
  }

} // namespace oxygen::interop::module

#pragma managed(pop)
