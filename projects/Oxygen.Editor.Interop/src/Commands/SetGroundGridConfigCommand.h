//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/GroundGridConfig.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  //! Sets how every view draws the ground grid.
  class SetGroundGridConfigCommand final : public EditorCommand {
  public:
    explicit SetGroundGridConfigCommand(
      const vortex::GroundGridConfig& config) noexcept
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , config_(config) {
    }

    void Execute(CommandContext& context) override {
      if (context.Renderer == nullptr) {
        DLOG_F(WARNING, "SetGroundGridConfigCommand: renderer unavailable");
        return;
      }
      context.Renderer->SetGroundGridConfig(config_);
    }

  private:
    vortex::GroundGridConfig config_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
