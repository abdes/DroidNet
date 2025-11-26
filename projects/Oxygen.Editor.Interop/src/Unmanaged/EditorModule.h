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
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>

#include "Unmanaged/SurfaceRegistry.h"

namespace Oxygen::Editor::EngineInterface {

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
        oxygen::core::PhaseId::kCommandRecord>();
    }

    auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
      -> bool override;
    auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
    auto OnCommandRecord(oxygen::engine::FrameContext& context)
      -> oxygen::co::Co<> override;

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

    // Keep track of indices at which we added our render surfaces to the frame
    // context, so that we can differentially update them each frame.
    std::unordered_map<const oxygen::graphics::Surface*, size_t>
      surface_indices_;
  };

} // namespace Oxygen::Editor::EngineInterface

#pragma managed(pop)
