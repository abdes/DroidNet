//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Core/PhaseRegistry.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  class ViewManager;

  //! Sets lens and clipping parameters for a view camera.
  class SetViewCameraSettingsCommand final : public EditorCommand {
  public:
    SetViewCameraSettingsCommand(
      ViewManager* manager,
      ViewId view_id,
      float field_of_view_y_radians,
      float near_plane,
      float far_plane) noexcept
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation),
      view_manager_(manager),
      view_id_(view_id),
      field_of_view_y_radians_(field_of_view_y_radians),
      near_plane_(near_plane),
      far_plane_(far_plane) {
    }

    void Execute(CommandContext& /*context*/) override;

  private:
    ViewManager* view_manager_;
    ViewId view_id_;
    float field_of_view_y_radians_;
    float near_plane_;
    float far_plane_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
