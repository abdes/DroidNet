// Minimal Graphics Facade for AsyncEngine coordinator
// Location: Examples/AsyncEngine/SubSystems
// Purpose: Provide a small, engine-only API to the Graphics subsystem.
// - No new handle types introduced
// - Uses primitive generation tokens (uint64_t)
// - Thin boundary: facade is coordinator-facing only; renderer/graphics own
//   per-handle validation and commit responsibilities.

#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include <Examples/AsyncEngine/Renderer/Graph/Types.h> // Temporary for FrameContext
#include <Oxygen/Base/NamedType.h>

namespace AsyncEngine {
namespace Subsystems {

  // We reuse the engine's FrameContext definition from the renderer example.

  class GraphicsFacade {
  public:
    // NOTE: Generation vs Epoch
    // - Generation: per-index counter embedded in VersionedHandle. Used to
    //   detect reuse of a specific bindless index. Validation MUST use the
    //   VersionedHandle.generation only.
    // - Epoch: global engine progress marker. Used for reclamation policies,
    //   diagnostics, and staleness heuristics (policy only). Do NOT use epoch
    //   as a substitute for per-handle generation validation.

    // NOTE: Resolution of VersionedHandles is a renderer responsibility. The
    // renderer builds its draw list from the Scene and resolves any
    // per-frame handle lists required by parallel tasks. This facade
    // deliberately exposes no APIs or types for per-frame handle storage;
    // renderer/graphics handle per-handle metadata and validation at commit
    // time using their own registry/allocator APIs.

    // Acquire a lightweight command/context for recording commands for the
    // given surface. Must be called in ordered phase to produce correct
    // submission ordering. NOTE: command recording is not an engine-core
    // responsibility; modules and clients should use the full Graphics Layer
    // API directly. The facade intentionally does not expose command context
    // acquisition or submission.

    // Lifecycle hooks the engine coordinator calls at ordered-phase boundaries.
    // The Graphics subsystem performs fence polling, reclamation, and any
    // begin/end-of-frame bookkeeping here.
    //
    // IMPORTANT API BOUNDARY NOTES:
    // - Do NOT add a separate `NotifyFrameStart` API: `BeginFrame` is the
    //   coordinator-facing entry point for frame-begin work and should be
    //   used by the coordinator instead. This facade intentionally keeps the
    //   ordered-phase lifecycle surface minimal.
    // - CBV/descriptor-table update, the atomic check+commit step (e.g.
    //   "ApplyPendingCBVUpdates" or similar), is owned by the renderer/graphics
    //   subsystem and MUST NOT be added to this facade. The renderer should
    //   perform validation and commits using its registry/allocator/graphics
    //   APIs during the CBV update pass (ordered-phase). This facade is for
    //   coordinator use only and therefore does not expose renderer-owned
    //   publishing queues or CBV-apply helpers.
    static void BeginFrame(const FrameContext& ctx);
    static void EndFrame(const FrameContext& ctx);
  };

} // namespace Subsystems
} // namespace AsyncEngine
