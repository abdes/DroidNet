//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstddef>
#include <string>
#include <utility>

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  class SetMaterialOverrideCommand : public EditorCommand {
  public:
    SetMaterialOverrideCommand(oxygen::scene::NodeHandle node,
      std::size_t slot_index, std::string material_uri)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , node_(node)
      , slot_index_(slot_index)
      , material_uri_(std::move(material_uri))
    {
    }

    void Execute(CommandContext& context) override;

  private:
    oxygen::scene::NodeHandle node_;
    std::size_t slot_index_ { 0 };
    std::string material_uri_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
