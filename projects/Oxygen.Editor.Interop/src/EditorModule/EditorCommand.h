//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Scene/Scene.h>

namespace oxygen::interop::module {

  //! Context passed to EditorCommands during execution.
  struct CommandContext {
    oxygen::scene::Scene* Scene;
    // Future: Add Renderer, AssetSystem, etc. if needed.
  };

  //! Abstract base class for all editor commands.
  class EditorCommand {
  public:
    virtual ~EditorCommand() = default;

    //! Executes the command logic.
    /*!
     @param context The context containing engine systems (Scene, etc.).
    */
    virtual void Execute(CommandContext& context) = 0;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
