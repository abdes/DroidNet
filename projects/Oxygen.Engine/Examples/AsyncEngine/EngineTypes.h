//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file EngineTypes.h
//! @brief Common lightweight types used throughout the AsyncEngine system
//!
//! This header contains frequently used data types and configuration structures
//! that are shared across multiple components in the AsyncEngine. By extracting
//! these types from AsyncEngineSimulator.h, we avoid forcing clients to include
//! the heavy simulator header and its dependencies just to access these types.
//!
//! Key types include:
//! - EngineProps: Engine configuration
//! - FrameSnapshot: Immutable frame state for parallel tasks
//! - RenderSurface: Surface description and state
//! - Task types: SyntheticTaskSpec, ParallelResult, AsyncJobState
//! - Performance metrics: FrameMetrics
//!
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace oxygen::engine::asyncsim {

//! Basic synthetic task categories.
enum class TaskCategory { Ordered, ParallelFrame, AsyncPipeline, Detached };

//! Specification for synthetic parallel tasks
struct SyntheticTaskSpec {
  std::string name;
  TaskCategory category { TaskCategory::ParallelFrame };
  std::chrono::microseconds cost { 1000 }; // simulated CPU time
};

//! Result from parallel task execution
struct ParallelResult {
  std::string name;
  std::chrono::microseconds duration { 0 };
};

//! State tracking for asynchronous background jobs
struct AsyncJobState {
  std::string name;
  std::chrono::microseconds remaining { 0 };
  uint64_t submit_frame { 0 };
  bool ready { false };
};

//! Per-frame performance metrics
struct FrameMetrics {
  uint64_t frame_index { 0 };
  std::chrono::microseconds frame_cpu_time { 0 };
  std::chrono::microseconds parallel_span { 0 };
  size_t parallel_jobs { 0 };
  size_t async_ready { 0 };
};

//! Engine configuration properties.
struct EngineProps {
  uint32_t target_fps { 0 }; //!< 0 = uncapped
};

//! Immutable per-frame snapshot passed to Category B parallel tasks
//! (placeholder for future scene/game state).
// NOTE: FrameSnapshot is now fully defined in FrameContext.h
// struct FrameSnapshot {
//   uint64_t frame_index { 0 };
// };

//! Represents a rendering surface with command recording state
struct RenderSurface {
  std::string name;
};

} // namespace oxygen::engine::asyncsim
