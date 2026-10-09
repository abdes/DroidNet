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

  //! Sets how a view presents the scene: its view mode and ground grid.
  class SetViewRenderOptionsCommand final : public EditorCommand {
  public:
    SetViewRenderOptionsCommand(ViewManager* manager, ViewId view_id,
      EditorViewRenderOptions options) noexcept
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , view_manager_(manager)
      , view_id_(view_id)
      , options_(options) {
    }

    //! Changes only what its own view shows.
    [[nodiscard]] auto GetInvalidation() const noexcept
      -> CommandInvalidation override {
      return CommandInvalidation::View(view_id_);
    }

    void Execute(CommandContext& /*context*/) override;

  private:
    ViewManager* view_manager_;
    ViewId view_id_;
    EditorViewRenderOptions options_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
