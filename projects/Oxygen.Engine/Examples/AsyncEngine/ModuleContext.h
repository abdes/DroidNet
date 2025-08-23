//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/OxCo/ThreadPool.h>

#include "EngineTypes.h"
#include "GraphicsLayer.h"

// Forward declarations for render graph integration
namespace oxygen::examples::asyncsim {
class RenderGraphModule;
class RenderGraphBuilder;
}

namespace oxygen::examples::asyncsim {

//! Frame execution context providing access to engine systems and data
//!
//! Provides controlled access to engine state based on current frame phase:
//! - Ordered phases: Full mutable access to authoritative state
//! - Parallel phases: Read-only snapshot access only
//! - Async phases: Versioned handles for safe multi-frame operations
//! - Detached phases: Minimal context for background work
class ModuleContext final {
public:
  explicit ModuleContext(uint64_t frame_index, oxygen::co::ThreadPool& pool,
    GraphicsLayer& graphics, const EngineProps& props) noexcept
    : frame_index_(frame_index)
    , pool_(pool)
    , graphics_(graphics)
    , props_(props)
  {
  }

  // Non-copyable, movable
  ModuleContext(const ModuleContext&) = delete;
  auto operator=(const ModuleContext&) -> ModuleContext& = delete;
  ModuleContext(ModuleContext&&) = default;
  auto operator=(ModuleContext&&) -> ModuleContext& = default;

  // === FRAME INFORMATION ===

  //! Current frame index (monotonically increasing)
  [[nodiscard]] uint64_t GetFrameIndex() const noexcept { return frame_index_; }

  //! Engine configuration properties
  [[nodiscard]] const EngineProps& GetEngineProps() const noexcept
  {
    return props_;
  }

  // === THREAD POOL ACCESS ===

  //! Thread pool for parallel/async work
  [[nodiscard]] oxygen::co::ThreadPool& GetThreadPool() noexcept
  {
    return pool_;
  }

  // === GRAPHICS LAYER ACCESS ===

  //! Graphics systems (resources, descriptors, rendering)
  [[nodiscard]] GraphicsLayer& GetGraphics() noexcept { return graphics_; }
  [[nodiscard]] const GraphicsLayer& GetGraphics() const noexcept
  {
    return graphics_;
  }

  // === SURFACE ACCESS (for multi-view render graph) ===
  void SetSurfacesPtr(const std::vector<RenderSurface>* surfaces) noexcept
  {
    surfaces_ = surfaces;
  }
  [[nodiscard]] const std::vector<RenderSurface>* GetSurfaces() const noexcept
  {
    return surfaces_;
  }

  // === RENDER GRAPH ACCESS ===

  //! Set render graph module reference (called during module registration)
  void SetRenderGraphModule(RenderGraphModule* render_graph_module) noexcept
  {
    render_graph_module_ = render_graph_module;
  }

  [[nodiscard]] RenderGraphModule* GetRenderGraphModule() const noexcept
  {
    return render_graph_module_;
  }

  //! Get render graph builder for current frame
  //! Only valid during FrameGraph phase when render graph module is active
  [[nodiscard]] RenderGraphBuilder* GetRenderGraphBuilder() noexcept;

  //! Check if render graph is available for this frame
  [[nodiscard]] bool HasRenderGraphAccess() const noexcept
  {
    return render_graph_module_ != nullptr
      && current_phase_ == FramePhase::FrameGraph;
  }

  // === SNAPSHOT ACCESS (Category B - Parallel phases only) ===

  //! Immutable frame snapshot for parallel work
  //! Only valid during parallel execution phases
  void SetFrameSnapshot(const FrameSnapshot* snapshot) noexcept
  {
    frame_snapshot_ = snapshot;
  }

  //! Get read-only frame snapshot
  //! Returns nullptr if not in parallel phase or snapshot not available
  [[nodiscard]] const FrameSnapshot* GetFrameSnapshot() const noexcept
  {
    return frame_snapshot_;
  }

  // === INPUT ACCESS ===

  //! Access to input state (implementation would provide input snapshot)
  //! For now, placeholder for future input system integration
  template <typename InputType>
  [[nodiscard]] const InputType* GetInput() const noexcept
  {
    // TODO: Implement input system integration
    // return input_system_.GetSnapshot<InputType>();
    return nullptr;
  }

  // === TIMING INFORMATION ===

  //! Frame timing information for adaptive systems
  struct FrameTiming {
    std::chrono::microseconds frame_duration { 0 };
    std::chrono::microseconds cpu_time { 0 };
    std::chrono::microseconds gpu_time { 0 };
    float cpu_usage_percent { 0.0f };
    float gpu_usage_percent { 0.0f };
  };

  void SetFrameTiming(const FrameTiming& timing) noexcept
  {
    frame_timing_ = timing;
  }
  [[nodiscard]] const FrameTiming& GetFrameTiming() const noexcept
  {
    return frame_timing_;
  }

  // === MODULE COMMUNICATION ===

  //! Simple message passing between modules (type-safe)
  template <typename MessageType> void PostMessage(const MessageType& message)
  {
    // TODO: Implement inter-module messaging system
    // message_bus_.Post(message);
  }

  template <typename MessageType>
  [[nodiscard]] std::vector<MessageType> GetMessages() const
  {
    // TODO: Implement inter-module messaging system
    // return message_bus_.GetMessages<MessageType>();
    return {};
  }

  // === PHASE-SPECIFIC ACCESS CONTROL ===

  //! Current frame phase (for debugging/validation)
  enum class FramePhase {
    Unknown,
    Input,
    FixedSimulation,
    Gameplay,
    NetworkReconciliation,
    SceneMutation,
    TransformPropagation,
    SnapshotBuild,
    ParallelWork,
    PostParallel,
    FrameGraph,
    DescriptorPublication,
    ResourceTransitions,
    CommandRecord,
    Present,
    AsyncWork,
    DetachedWork
  };

  void SetCurrentPhase(FramePhase phase) noexcept { current_phase_ = phase; }
  [[nodiscard]] FramePhase GetCurrentPhase() const noexcept
  {
    return current_phase_;
  }

  //! Check if we're in a phase that allows mutable state access
  [[nodiscard]] bool CanMutateState() const noexcept
  {
    return current_phase_
      != FramePhase::ParallelWork; // Parallel phase is read-only
  }

  //! Check if we're in a phase that provides snapshot access
  [[nodiscard]] bool HasSnapshotAccess() const noexcept
  {
    return current_phase_ == FramePhase::ParallelWork
      && frame_snapshot_ != nullptr;
  }

private:
  uint64_t frame_index_;
  oxygen::co::ThreadPool& pool_;
  GraphicsLayer& graphics_;
  const EngineProps& props_;
  const FrameSnapshot* frame_snapshot_ { nullptr };
  FrameTiming frame_timing_ {};
  FramePhase current_phase_ { FramePhase::Unknown };

  // Render graph integration
  RenderGraphModule* render_graph_module_ { nullptr };
  const std::vector<RenderSurface>* surfaces_ { nullptr };
};

} // namespace oxygen::examples::asyncsim
