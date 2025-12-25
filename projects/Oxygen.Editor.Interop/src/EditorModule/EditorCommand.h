//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen {
  class Graphics;

  namespace engine {
    class FrameContext;
  }

  namespace content {
    class AssetLoader;
    class VirtualPathResolver;
  }
} // namespace oxygen

namespace oxygen::interop::module {

  //! Context passed to EditorCommands during execution.
  struct CommandContext {
    // Use observer_ptr to make volatility explicit (command handlers must
    // not retain or store the pointer beyond execution).
    oxygen::observer_ptr<oxygen::scene::Scene> Scene;
    oxygen::observer_ptr<oxygen::content::AssetLoader> AssetLoader;
    oxygen::observer_ptr<oxygen::content::VirtualPathResolver> PathResolver;
  };

  //! Abstract base class for all editor commands.
  class EditorCommand {
  public:
    // Require callers to explicitly choose the phase for the command. There
    // is no default because command authors must consciously decide the
    // execution phase (FrameStart vs SceneMutation etc.).
    explicit EditorCommand(oxygen::core::PhaseId phase) noexcept
      : target_phase_(phase) {
    }

    virtual ~EditorCommand() = default;

    //! Executes the command logic.
    /*!
     @param context The context containing engine systems (Scene, etc.).
    */
    virtual void Execute(CommandContext& context) = 0;

    [[nodiscard]] auto GetTargetPhase() const noexcept -> oxygen::core::PhaseId {
      return target_phase_;
    }

  private:
    oxygen::core::PhaseId target_phase_{ oxygen::core::PhaseId::kSceneMutation };
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
