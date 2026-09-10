//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <functional>
#include <utility>

#include <EditorModule/EditorCommand.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::interop::module {

//! Scene-owned solid sky state sampled during the mutation phase.
struct BackgroundObservation {
  bool exists = false;
  Vec3 color { 0.0F, 0.0F, 0.0F };
  bool atmosphere_enabled = false;
};

//! Applies authored linear RGB through the engine's public SkySphere API.
class SetBackgroundColorCommand final : public EditorCommand {
public:
  //! Captures the immutable color payload for mutation-phase application.
  explicit SetBackgroundColorCommand(Vec3 color)
    : EditorCommand(core::PhaseId::kSceneMutation), color_(color) {}

  //! Updates the scene's solid sky without changing atmosphere selection.
  void Execute(CommandContext& context) override;

private:
  Vec3 color_;
};

//! Observes background state without interpreting it as presented pixels.
class ObserveBackgroundCommand final : public EditorCommand {
public:
  //! Retains the observer until execution or command destruction.
  explicit ObserveBackgroundCommand(
    std::function<void(BackgroundObservation)> complete)
    : EditorCommand(core::PhaseId::kSceneMutation)
    , complete_(std::move(complete)) {}

  //! Returns the scene-owned state after earlier queued mutations.
  void Execute(CommandContext& context) override;

private:
  std::function<void(BackgroundObservation)> complete_;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
