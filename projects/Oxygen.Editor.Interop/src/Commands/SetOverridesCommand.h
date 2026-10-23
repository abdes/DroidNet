//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <utility>
#include <vector>

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/SceneAssetRequests.h>
#include <api_export.h>

namespace oxygen::interop::module {

  //! Sets the whole override state of one part of a node's geometry: a
  //! surface, or the geometry component itself. A property of the part with no
  //! entry takes the geometry's asset value.
  class SetOverridesCommand final : public EditorCommand {
  public:
    SetOverridesCommand(oxygen::scene::NodeHandle node, OverrideTarget target,
      std::vector<OverrideEntry> entries, OverrideIntent intent)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , node_(node)
      , target_(std::move(target))
      , entries_(std::move(entries))
      , intent_(intent)
    {
    }

    OXGN_EI_API void Execute(CommandContext& context) override;

    //! Installs the observer of this override set's failures.
    void SetFailureCallback(SceneAssetRequests::FailureCallback callback) {
      failure_callback_ = std::move(callback);
    }

    //! Observes application of this override set and its refreshes.
    void SetSuccessCallback(SceneAssetRequests::SuccessCallback callback) {
      success_callback_ = std::move(callback);
    }

  private:
    oxygen::scene::NodeHandle node_;
    OverrideTarget target_;
    std::vector<OverrideEntry> entries_;
    OverrideIntent intent_ = OverrideIntent::kObservedEdit;
    SceneAssetRequests::FailureCallback failure_callback_;
    SceneAssetRequests::SuccessCallback success_callback_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
