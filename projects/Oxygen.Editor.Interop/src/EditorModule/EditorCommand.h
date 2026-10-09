//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstdint>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen {
  class Graphics;
  namespace vortex {
    class Renderer;
  }

  namespace engine {
    class FrameContext;
  }

  namespace content {
    class IAssetLoader;
    class VirtualPathResolver;
  }
} // namespace oxygen

namespace oxygen::interop::module {

  class SceneAssetRequests;

  //! Context passed to EditorCommands during execution.
  struct CommandContext {
    // Use observer_ptr to make volatility explicit (command handlers must
    // not retain or store the pointer beyond execution).
    oxygen::observer_ptr<oxygen::engine::FrameContext> FrameContext;
    oxygen::observer_ptr<oxygen::scene::Scene> Scene;
    oxygen::observer_ptr<oxygen::content::IAssetLoader> AssetLoader;
    oxygen::observer_ptr<oxygen::content::VirtualPathResolver> PathResolver;
    oxygen::observer_ptr<SceneAssetRequests> AssetRequests;
    oxygen::observer_ptr<oxygen::vortex::Renderer> Renderer;
  };

  //! What executing a command can change in the viewport panes.
  struct CommandInvalidation {
    enum class Scope : std::uint8_t {
      kNone, //!< Nothing any pane shows.
      kView, //!< Only what `view` shows.
      kScene, //!< Anything every pane shows.
    };

    Scope scope { Scope::kScene };
    ViewId view { kInvalidViewId };

    [[nodiscard]] static constexpr auto None() noexcept -> CommandInvalidation {
      return { .scope = Scope::kNone, .view = kInvalidViewId };
    }
    [[nodiscard]] static constexpr auto View(const ViewId view_id) noexcept
      -> CommandInvalidation {
      return { .scope = Scope::kView, .view = view_id };
    }
    [[nodiscard]] static constexpr auto Scene() noexcept
      -> CommandInvalidation {
      return { .scope = Scope::kScene, .view = kInvalidViewId };
    }
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

    //! What this command's execution can change on screen. Commands that
    //! touch the scene keep the default; view-only and read-only commands
    //! narrow it so idle panes are not re-rendered.
    [[nodiscard]] virtual auto GetInvalidation() const noexcept
      -> CommandInvalidation {
      return CommandInvalidation::Scene();
    }

  private:
    oxygen::core::PhaseId target_phase_{ oxygen::core::PhaseId::kSceneMutation };
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
