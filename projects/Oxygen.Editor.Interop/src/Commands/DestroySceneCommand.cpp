//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <Commands/DestroySceneCommand.h>
#include <EditorModule/EditorCommand.h>
#include <EditorModule/EditorModule.h>

namespace oxygen::interop::module {

void DestroySceneCommand::Execute(CommandContext& context) {
  if (!module_)
    return;

  try {
    module_->ApplyDestroyScene();
  }
  catch (...) {
    // swallow exceptions across engine boundary
  }
}

} // namespace oxygen::interop::module
