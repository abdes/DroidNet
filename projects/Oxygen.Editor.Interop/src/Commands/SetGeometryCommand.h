//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <string>
#include <utility>

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/SceneAssetRequests.h>

namespace oxygen::interop::module {

  class SetGeometryCommand : public EditorCommand {
  public:
    SetGeometryCommand(oxygen::scene::NodeHandle node, std::string assetUri)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation), node_(node),
      assetUri_(std::move(assetUri)) {
    }

    void Execute(CommandContext& context) override;

    //! Installs the observer carried by this specific geometry intent.
    void SetFailureCallback(SceneAssetRequests::FailureCallback callback) {
      failure_callback_ = std::move(callback);
    }

    //! Observes application of this geometry intent and its refreshes.
    void SetSuccessCallback(SceneAssetRequests::SuccessCallback callback) {
      success_callback_ = std::move(callback);
    }

  private:
    oxygen::scene::NodeHandle node_;
    std::string assetUri_;
    SceneAssetRequests::FailureCallback failure_callback_;
    SceneAssetRequests::SuccessCallback success_callback_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
