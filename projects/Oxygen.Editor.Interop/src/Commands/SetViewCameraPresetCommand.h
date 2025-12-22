//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Core/PhaseRegistry.h>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/EditorView.h>

namespace oxygen::interop::module {

  class ViewManager;

  //! Sets a view camera to a preset (Perspective/Top/Bottom/Left/Right/Front/Back).
  class SetViewCameraPresetCommand final : public EditorCommand {
  public:
    SetViewCameraPresetCommand(ViewManager* manager, ViewId view_id,
      CameraViewPreset preset) noexcept
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation),
      view_manager_(manager),
      view_id_(view_id),
      preset_(preset) {
    }

    void Execute(CommandContext& /*context*/) override;

  private:
    ViewManager* view_manager_;
    ViewId view_id_;
    CameraViewPreset preset_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
