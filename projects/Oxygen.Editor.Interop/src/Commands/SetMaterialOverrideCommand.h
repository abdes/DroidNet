//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/SceneAssetRequests.h>

namespace oxygen::interop::module {

  class SetMaterialOverrideCommand final : public EditorCommand {
  public:
    SetMaterialOverrideCommand(oxygen::scene::NodeHandle node,
      MaterialSlotTarget target, std::optional<std::string> material_uri,
      MaterialSlotAssignmentIntent intent)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , node_(node)
      , target_(std::move(target))
      , material_uri_(std::move(material_uri))
      , intent_(intent)
    {
    }

    void Execute(CommandContext& context) override;

    //! Installs the observer carried by this specific material-slot intent.
    void SetFailureCallback(SceneAssetRequests::FailureCallback callback) {
      failure_callback_ = std::move(callback);
    }

    //! Observes application of this material intent and its refreshes.
    void SetSuccessCallback(SceneAssetRequests::SuccessCallback callback) {
      success_callback_ = std::move(callback);
    }

  private:
    oxygen::scene::NodeHandle node_;
    MaterialSlotTarget target_;
    std::optional<std::string> material_uri_;
    MaterialSlotAssignmentIntent intent_ = MaterialSlotAssignmentIntent::kObservedEdit;
    SceneAssetRequests::FailureCallback failure_callback_;
    SceneAssetRequests::SuccessCallback success_callback_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
