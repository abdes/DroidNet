//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <functional>
#include <optional>

#include <Oxygen/Core/PhaseRegistry.h>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/EditorView.h>

namespace oxygen::interop::module {

  class ViewManager;

  //! Reports a view's editor camera state, so a pane can keep it across view
  //! recreation and sessions.
  /*!
   The callback always runs exactly once, on the engine thread, with no value
   when the view does not exist.
  */
  class QueryViewEditorCameraCommand final : public EditorCommand {
  public:
    using Callback = std::function<void(std::optional<EditorCameraState>)>;

    QueryViewEditorCameraCommand(
      ViewManager* manager, ViewId view_id, Callback callback) noexcept
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , view_manager_(manager)
      , view_id_(view_id)
      , callback_(std::move(callback))
    {
    }

    ~QueryViewEditorCameraCommand() override;

    OXYGEN_MAKE_NON_COPYABLE(QueryViewEditorCameraCommand)
    OXYGEN_MAKE_NON_MOVABLE(QueryViewEditorCameraCommand)

    //! Only reads a camera pose; changes nothing a pane shows.
    [[nodiscard]] auto GetInvalidation() const noexcept
      -> CommandInvalidation override {
      return CommandInvalidation::None();
    }

    void Execute(CommandContext& context) override;

  private:
    void Complete(std::optional<EditorCameraState> state);

    ViewManager* view_manager_;
    ViewId view_id_;
    Callback callback_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
