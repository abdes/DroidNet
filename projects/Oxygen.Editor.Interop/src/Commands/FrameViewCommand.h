//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <functional>
#include <vector>

#include <Oxygen/Core/PhaseRegistry.h>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/EditorView.h>
#include <EditorModule/NodeRegistry.h>

namespace oxygen::interop::module {

  class ViewManager;

  //! Frames scene nodes, or the whole scene, in one view.
  /*!
   An empty node list frames the whole scene. The callback always runs
   exactly once, on the engine thread, also when the command is dropped
   unexecuted (`kNoView`).
  */
  class FrameViewCommand final : public EditorCommand {
  public:
    using Callback = std::function<void(EditorFramingOutcome)>;

    FrameViewCommand(ViewManager* manager, ViewId view_id,
      std::vector<UuidKey> nodes, Callback callback) noexcept
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , view_manager_(manager)
      , view_id_(view_id)
      , nodes_(std::move(nodes))
      , callback_(std::move(callback))
    {
    }

    ~FrameViewCommand() override;

    OXYGEN_MAKE_NON_COPYABLE(FrameViewCommand)
    OXYGEN_MAKE_NON_MOVABLE(FrameViewCommand)

    //! Changes only what its own view shows.
    [[nodiscard]] auto GetInvalidation() const noexcept
      -> CommandInvalidation override {
      return CommandInvalidation::View(view_id_);
    }

    void Execute(CommandContext& context) override;

  private:
    void Complete(EditorFramingOutcome outcome);

    ViewManager* view_manager_;
    ViewId view_id_;
    std::vector<UuidKey> nodes_;
    Callback callback_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
