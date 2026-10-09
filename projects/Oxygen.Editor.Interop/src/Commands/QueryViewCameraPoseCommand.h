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
#include <EditorModule/NodeRegistry.h>

namespace oxygen::interop::module {

  class ViewManager;

  //! Reports the pose that would place a scene node at a view's editor camera.
  /*!
   The callback always runs exactly once, on the engine thread, with no value
   when the view or the node does not exist.
  */
  class QueryViewCameraPoseCommand final : public EditorCommand {
  public:
    using Callback = std::function<void(std::optional<EditorCameraPose>)>;

    QueryViewCameraPoseCommand(ViewManager* manager, ViewId view_id,
      UuidKey node_id, Callback callback) noexcept
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , view_manager_(manager)
      , view_id_(view_id)
      , node_id_(node_id)
      , callback_(std::move(callback))
    {
    }

    ~QueryViewCameraPoseCommand() override;

    OXYGEN_MAKE_NON_COPYABLE(QueryViewCameraPoseCommand)
    OXYGEN_MAKE_NON_MOVABLE(QueryViewCameraPoseCommand)

    //! Only reads a camera pose; changes nothing a pane shows.
    [[nodiscard]] auto GetInvalidation() const noexcept
      -> CommandInvalidation override {
      return CommandInvalidation::None();
    }

    void Execute(CommandContext& context) override;

  private:
    void Complete(std::optional<EditorCameraPose> pose);

    ViewManager* view_manager_;
    ViewId view_id_;
    UuidKey node_id_;
    Callback callback_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
