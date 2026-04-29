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

  //! Sets the base fly movement speed for a view camera.
  class SetViewCameraMovementSpeedCommand final : public EditorCommand {
  public:
    SetViewCameraMovementSpeedCommand(
      ViewManager* manager,
      ViewId view_id,
      float speed_units_per_second) noexcept
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation),
      view_manager_(manager),
      view_id_(view_id),
      speed_units_per_second_(speed_units_per_second) {
    }

    void Execute(CommandContext& /*context*/) override;

  private:
    ViewManager* view_manager_;
    ViewId view_id_;
    float speed_units_per_second_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
