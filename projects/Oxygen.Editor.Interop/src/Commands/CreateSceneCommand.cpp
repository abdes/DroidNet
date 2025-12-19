//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <Commands/CreateSceneCommand.h>
#include <EditorModule/EditorCommand.h>
#include <EditorModule/EditorModule.h>

namespace oxygen::interop::module {

  void CreateSceneCommand::Execute(CommandContext& context) {
    if (!module_)
      return;

    try {
      module_->ApplyCreateScene(name_);
      if (cb_) {
        try { cb_(true); }
        catch (...) { /* swallow */ }
      }
    }
    catch (...) {
      // swallow: command should not throw across engine boundary
      if (cb_) {
        try { cb_(false); }
        catch (...) {}
      }
    }
  }

} // namespace oxygen::interop::module
