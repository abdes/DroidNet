//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Core/PhaseRegistry.h>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/EditorViewportCameraControlMode.h>
#include <EditorModule/EditorView.h>

namespace oxygen::interop::module {

  class ViewManager;

  //! Sets the editor camera navigation mode for a viewport.
  class SetViewCameraControlModeCommand final : public EditorCommand {
  public:
    SetViewCameraControlModeCommand(ViewManager* manager, ViewId view_id,
      EditorViewportCameraControlMode mode) noexcept
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation),
        view_manager_(manager),
        view_id_(view_id),
        mode_(mode) {
    }

    void Execute(CommandContext& /*context*/) override;

  private:
    ViewManager* view_manager_;
    ViewId view_id_;
    EditorViewportCameraControlMode mode_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
