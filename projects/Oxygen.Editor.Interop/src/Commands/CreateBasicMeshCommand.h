//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <string>
#include <utility>

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  class CreateBasicMeshCommand : public EditorCommand {
  public:
    CreateBasicMeshCommand(oxygen::scene::NodeHandle node, std::string meshType)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation), node_(node),
      meshType_(std::move(meshType)) {
    }

    void Execute(CommandContext& context) override;

  private:
    oxygen::scene::NodeHandle node_;
    std::string meshType_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
