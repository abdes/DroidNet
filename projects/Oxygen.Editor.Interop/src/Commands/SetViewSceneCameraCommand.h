//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <optional>

#include <Oxygen/Core/PhaseRegistry.h>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/NodeRegistry.h>

namespace oxygen::interop::module {

  class ViewManager;

  //! Renders a view through an authored scene camera, or through its editor
  //! camera when no node is given.
  class SetViewSceneCameraCommand final : public EditorCommand {
  public:
    SetViewSceneCameraCommand(ViewManager* manager, ViewId view_id,
      std::optional<UuidKey> camera_node_id) noexcept
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation),
      view_manager_(manager),
      view_id_(view_id),
      camera_node_id_(camera_node_id) {
    }

    void Execute(CommandContext& /*context*/) override;

  private:
    ViewManager* view_manager_;
    ViewId view_id_;
    std::optional<UuidKey> camera_node_id_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
