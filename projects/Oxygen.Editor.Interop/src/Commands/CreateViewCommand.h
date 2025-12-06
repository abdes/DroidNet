//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <functional>
#include <utility>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/PhaseRegistry.h>

#include "EditorModule/EditorCommand.h"
#include "EditorModule/EditorView.h"

namespace oxygen::interop::module {

  class ViewManager;

  class CreateViewCommand final : public EditorCommand {
  public:
    // Use an explicit function signature here so this header does not depend
    // on ViewManager being fully defined at this point (avoids any ordering
    // issues when compiled in mixed managed/unmanaged contexts).
    using OnViewCreated = std::function<void(bool success, ViewId engine_id)>;

    CreateViewCommand(ViewManager* manager, EditorView::Config cfg,
      OnViewCreated cb)
      : EditorCommand(oxygen::core::PhaseId::kFrameStart), view_manager_(manager),
      cfg_(std::move(cfg)), cb_(std::move(cb)) {
    }

    // Execute during FrameStart: request immediate creation via ViewManager.
    // The ViewManager will use the active FrameContext (provided by
    // EditorModule::OnFrameStart) to register the view. Note commands do not
    // receive a FrameContext argument.
    void Execute(CommandContext& context) override;

  private:
    ViewManager* view_manager_;
    EditorView::Config cfg_;
    OnViewCreated cb_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
