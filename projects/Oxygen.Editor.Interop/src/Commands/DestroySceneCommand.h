//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <EditorModule/EditorCommand.h>
#include <Oxygen/Core/PhaseRegistry.h>

namespace oxygen::interop::module {

class EditorModule;

class DestroySceneCommand final : public EditorCommand {
public:
  DestroySceneCommand(EditorModule* module)
    : EditorCommand(oxygen::core::PhaseId::kFrameStart), module_(module) {}

  void Execute(CommandContext& context) override;

private:
  EditorModule* module_;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
