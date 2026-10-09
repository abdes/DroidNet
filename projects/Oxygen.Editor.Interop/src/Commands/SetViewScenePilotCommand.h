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

  //! Starts or stops piloting the scene camera a view looks through.
  class SetViewScenePilotCommand final : public EditorCommand {
  public:
    SetViewScenePilotCommand(
      ViewManager* manager, ViewId view_id, bool pilot) noexcept
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , view_manager_(manager)
      , view_id_(view_id)
      , pilot_(pilot)
    {
    }

    //! Changes only what its own view shows.
    [[nodiscard]] auto GetInvalidation() const noexcept
      -> CommandInvalidation override {
      return CommandInvalidation::View(view_id_);
    }

    void Execute(CommandContext& context) override;

  private:
    ViewManager* view_manager_;
    ViewId view_id_;
    bool pilot_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
