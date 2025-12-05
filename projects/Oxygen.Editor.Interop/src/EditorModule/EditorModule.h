//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <memory>
#include <unordered_map>
#include <vector>

#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/TransparentPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Scene/Scene.h>

#include "EditorModule/EditorCommand.h"
#include "EditorModule/EditorCompositor.h"
#include "EditorModule/SurfaceRegistry.h"
#include "EditorModule/ThreadSafeQueue.h"
#include "EditorModule/ViewManager.h"

namespace oxygen::interop::module {

  class RenderGraph; // forward-declare the helper at namespace scope

  //! An engine module, that connects the editor to the Oxygen engine.
  /*!
   Because this is an engine modules, it is fully aware of the frame lifecycle,
   can execute certain actions on the engine thread and exactly at a specific
   phase. Avoids the needs to expose lower level primitives from the engine to
   do frame synchronization.

   Consitently with the Oxygen engine architecture, this module acts as an
   application module, owning the application specific logic and data, and the
   surfaces used for rendering and presentation.

   @note
   In this particular implementation, surface/swapchain management is delegated
   to a `SurfaceRegistry` instance, which acts as a thread-safe surface manager,
   with lazy creation, deferred destruction and reuse of surfaces between
   multiple viewports as needed. The module is still howver, the single point of
   contact between the editor and the engine when it comes to surface lifecycle.
  */
  class EditorModule final : public oxygen::engine::EngineModule {
    OXYGEN_TYPED(EditorModule)
  public:
    //! Constructs the editor module with the provided surface registry, which
    //! must not be `null`.
    /*!
     @throws std::invalid_argument if `registry` is `null`.
    */
    explicit EditorModule(std::shared_ptr<SurfaceRegistry> registry);

    ~EditorModule() override;

    [[nodiscard]] auto GetName() const noexcept -> std::string_view override {
      return "EditorModule";
    }

    [[nodiscard]] auto GetPriority() const noexcept
      -> oxygen::engine::ModulePriority override {
      return oxygen::engine::kModulePriorityHighest;
    }

    [[nodiscard]] auto GetSupportedPhases() const noexcept
      -> oxygen::engine::ModulePhaseMask override {
      return oxygen::engine::MakeModuleMask<oxygen::core::PhaseId::kFrameStart,
        oxygen::core::PhaseId::kPreRender,
        oxygen::core::PhaseId::kRender,
        oxygen::core::PhaseId::kCompositing,
        oxygen::core::PhaseId::kSceneMutation>();
    }

    auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
      -> bool override;
    auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
    auto OnSceneMutation(oxygen::engine::FrameContext& context)
      -> oxygen::co::Co<> override;
    auto OnPreRender(oxygen::engine::FrameContext& context)
      -> oxygen::co::Co<> override;
    auto OnRender(oxygen::engine::FrameContext& context)
      -> oxygen::co::Co<> override;
    auto OnCompositing(oxygen::engine::FrameContext& context)
      -> oxygen::co::Co<> override;

    // Ensure framebuffers for all registered surfaces (creates depth textures
    // and one framebuffer per backbuffer slot). Mirrors
    // AppWindow::EnsureFramebuffers from the examples so editor behavior matches
    // the sample exactly.
    auto EnsureFramebuffers() -> bool;

    // Scene management API
    auto CreateScene(std::string_view name) -> void;

    //! Enqueues a command to be executed during the SceneMutation phase.
    void Enqueue(std::unique_ptr<EditorCommand> cmd);

  private:
    void ProcessSurfaceRegistrations();
    void ProcessSurfaceDestructions();
    auto ProcessResizeRequests()
      -> std::vector<std::shared_ptr<oxygen::graphics::Surface>>;
    auto SyncSurfacesWithFrameContext(
      oxygen::engine::FrameContext& context,
      const std::vector<std::shared_ptr<oxygen::graphics::Surface>>& surfaces)
      -> void;

    std::shared_ptr<SurfaceRegistry> registry_;
    std::weak_ptr<oxygen::Graphics> graphics_;
    oxygen::observer_ptr<oxygen::AsyncEngine> engine_{};

    std::shared_ptr<oxygen::scene::Scene> scene_;

    std::chrono::steady_clock::time_point last_frame_time_{};

    // Command queue for scene mutations
    ThreadSafeQueue<std::unique_ptr<EditorCommand>> command_queue_;

    // New Architecture Components
    std::unique_ptr<ViewManager> view_manager_;
    std::unique_ptr<EditorCompositor> compositor_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
