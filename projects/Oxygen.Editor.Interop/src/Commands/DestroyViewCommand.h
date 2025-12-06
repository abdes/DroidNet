//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Types/View.h>

#include "EditorModule/EditorCommand.h"

namespace oxygen::interop::module {

  class ViewManager;

  class DestroyViewCommand final : public EditorCommand {
  public:
    DestroyViewCommand(ViewManager* mgr, ViewId id)
      : EditorCommand(oxygen::core::PhaseId::kFrameStart), view_manager_(mgr),
      view_id_(id) {
    }
    void Execute(CommandContext& ctx) override;

  private:
    ViewManager* view_manager_{ nullptr };
    ViewId view_id_{};
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
