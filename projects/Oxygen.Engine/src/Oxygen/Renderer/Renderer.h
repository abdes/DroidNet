//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Engine/Modules/EngineModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/CameraView.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::scene {
class Scene;
}

namespace oxygen::graphics {
class Buffer;
} // namespace oxygen::graphics

namespace oxygen::data {
class Mesh;
} // namespace oxygen::data

namespace oxygen::engine {

namespace sceneprep {
  class ScenePrepState;
  class ScenePrepPipeline;
} // namespace sceneprep

struct RenderContext;
struct MaterialConstants;

//! Renderer: backend-agnostic, manages mesh-to-GPU resource mapping and
//! eviction.
class Renderer : public EngineModule {
  OXYGEN_TYPED(Renderer)

public:
  // Renderer must be constructed with a valid RendererConfig containing a
  // non-empty upload_queue_key key.
  OXGN_RNDR_API explicit Renderer(
    std::weak_ptr<Graphics> graphics, RendererConfig config);

  OXYGEN_MAKE_NON_COPYABLE(Renderer)
  OXYGEN_DEFAULT_MOVABLE(Renderer)

  OXGN_RNDR_API ~Renderer() override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "RendererModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept -> ModulePriority override
  {
    // This module must run last, after all modules that may contribute to the
    // frame context.
    return ModulePriority { kModulePriorityLowest };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> ModulePhaseMask override
  {
    // Participate in frame start, transform propagation and command record.
    return MakeModuleMask<core::PhaseId::kFrameStart,
      core::PhaseId::kTransformPropagation, core::PhaseId::kFrameGraph,
      core::PhaseId::kCommandRecord>();
  }

  OXGN_RNDR_NDAPI auto OnFrameStart(FrameContext& context) -> void override;

  OXGN_RNDR_NDAPI auto OnTransformPropagation(FrameContext& context)
    -> co::Co<> override;

  OXGN_RNDR_NDAPI auto OnFrameGraph(FrameContext& context) -> co::Co<> override;

  // Submit deferred uploads and retire completed ones during command record.
  OXGN_RNDR_NDAPI auto OnCommandRecord(FrameContext& context)
    -> co::Co<> override;

  //! Executes a render graph coroutine with the given context.
  template <typename RenderGraphCoroutine>
  auto ExecuteRenderGraph(RenderGraphCoroutine&& graph_coroutine,
    RenderContext& render_context, const FrameContext& frame_context)
    -> co::Co<>
  {
    // If the renderer encounters fatal errors while preparing the scene or the
    // frame, then this is a garbage frame that should not be rendered.
    if (skip_frame_render_) {
      co_return;
    }

    PreExecute(render_context, frame_context);
    co_await std::forward<RenderGraphCoroutine>(graph_coroutine)(
      render_context);
    PostExecute(render_context);
  }

  // Legacy AoS accessors removed (opaque_items_ no longer drives frame build).
  // Temporary: any external caller relying on OpaqueItems()/GetOpaqueItems()
  // must migrate to PreparedSceneFrame consumption.

  //! Returns the Graphics system used by this renderer.
  OXGN_RNDR_API auto GetGraphics() -> std::shared_ptr<Graphics>;

  //! Modify scene constants in-place via a user-provided mutator.
  /*!
    The mutator is invoked with a reference to the internal SceneConstants
    instance. Use the chainable SceneConstants setters inside the mutator so
    versioning and lazy snapshotting are preserved.
  */
  OXGN_RNDR_API auto ModifySceneConstants(
    const std::function<void(SceneConstants&)>& mutator) -> void;
  //! Returns the last set scene constants (undefined before first set).
  OXGN_RNDR_API auto GetSceneConstants() const -> const SceneConstants&;

  //! Accessor for in-progress SoA frame snapshot (Task 6+). Returns an empty
  //! frame until finalization is wired.
  [[nodiscard]] auto GetPreparedFrame() const noexcept
    -> const PreparedSceneFrame&
  {
    return prepared_frame_;
  }

  OXGN_RNDR_API auto BuildFrame(
    const View& view, const FrameContext& frame_context) -> std::size_t;

  OXGN_RNDR_API auto BuildFrame(const renderer::CameraView& camera_view,
    const FrameContext& frame_context) -> std::size_t;

private:
  OXGN_RNDR_API auto PreExecute(
    RenderContext& context, const FrameContext& frame_context) -> void;
  OXGN_RNDR_API auto PostExecute(RenderContext& context) -> void;

  //! Update scene constants from resolved view matrices & camera state.
  auto UpdateSceneConstantsFromView(const View& view) -> void;
  //! Compute current finalized draw count (post-sort) from prepared frame.
  auto CurrentDrawCount() const noexcept -> std::size_t;

  //! Publish spans into PreparedSceneFrame using TransformUploader data
  //! directly.
  auto PublishPreparedFrameSpans() -> void;

  auto MaybeUpdateSceneConstants(const FrameContext& frame_context) -> void;

  //! Wires updated buffers into the provided render context for the frame.
  auto WireContext(RenderContext& context) -> void;

  std::weak_ptr<Graphics> gfx_weak_; // New AsyncEngine path

  // Managed draw item container removed (AoS path deprecated).

  // Scene constants management
  std::shared_ptr<graphics::Buffer> scene_const_buffer_;
  SceneConstants scene_const_cpu_;
  MonotonicVersion last_uploaded_scene_const_version_ { (
    std::numeric_limits<uint64_t>::max)() };

  // CPU-owning storage populated during finalization each frame. Spans inside
  // PreparedSceneFrame alias these vectors (no ownership transfer).
  PreparedSceneFrame prepared_frame_ {}; // view object
  // if true, skip rendering this frame, either because it's garbage due to
  // errors, or because it has no draws.
  bool skip_frame_render_ { false };

  // Persistent ScenePrep state (caches transforms/materials/geometry across
  // frames). ResetFrameData() is invoked each BuildFrame while retaining
  // deduplicated caches inside contained managers.
  std::unique_ptr<sceneprep::ScenePrepState> scene_prep_state_;
  std::unique_ptr<sceneprep::ScenePrepPipeline> scene_prep_;

  // Frame sequence number from FrameContext
  frame::SequenceNumber frame_seq_num { 0ULL };

  // Upload coordinator: manages buffer/texture uploads and completion.
  std::unique_ptr<upload::UploadCoordinator> uploader_;
  std::shared_ptr<upload::StagingProvider> staging_provider_;
};

} // namespace oxygen::engine
