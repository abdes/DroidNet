//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Types/View.h>

#include "../Integration/GraphicsLayerIntegration.h"

namespace oxygen::engine::asyncsim {

// Forward declarations
class TaskExecutionContext;

//! Enumerations for render graph system
//! NOTE: Previously these enums were inside an anonymous namespace giving them
//! internal linkage per TU. That risks ODR mismatches and prevents their safe
//! use across translation units. They are now promoted to the public namespace
//! for consistent strong typing engine-wide.

//! Resource scope determines sharing across views
enum class ResourceScope : uint32_t {
  Shared, //!< Resource computed once and used by all views (shadows, lighting
          //!< data)
  PerView //!< Resource that is view-specific (depth buffers, color buffers)
};

//! Resource lifetime controls memory aliasing and pooling
enum class ResourceLifetime : uint32_t {
  FrameLocal, //!< Resources that live for the entire frame
  Transient, //!< Resources that can be aliased after their last use
  External //!< External resources managed outside the render graph
};

//! Pass scope determines execution pattern
enum class PassScope : uint32_t {
  Shared, //!< Passes that run once for all views (shadow mapping, light
          //!< culling)
  PerView, //!< Passes that run independently for each view
  Viewless //!< Passes that run once without needing view context (compute,
           //!< streaming)
};

//! Queue type for GPU command submission
enum class QueueType : uint32_t {
  Graphics, //!< Graphics queue for rendering operations
  Compute, //!< Compute queue for compute shader work
  Copy //!< Copy queue for resource transfers
};

//! Priority levels for pass execution scheduling
enum class Priority : uint32_t {
  Critical, //!< Highest priority - must execute first
  High, //!< High priority for critical path work
  Normal, //!< Standard priority for most work
  Low, //!< Lower priority for non-critical work
  Background //!< Lowest priority for background tasks
};

// TODO(Phase2): Consider adding explicit serialization helpers for these enums
// to support caching & hot-reload of compiled graphs.

//! Strong-typed handle for render passes
// clang-format off
using PassHandle = oxygen::NamedType<
  uint32_t,
  struct PassHandleTag,
  oxygen::Hashable,
  oxygen::Comparable,
  oxygen::Printable
>;
// clang-format on

//! Strong-typed handle for view identifiers
// clang-format off
using ViewId = oxygen::NamedType<
  uint32_t,
  struct ViewIdTag,
  oxygen::Hashable,
  oxygen::Comparable,
  oxygen::Printable
>;
// clang-format on

//! Pass executor function type - synchronous command recording only
/*!
 Pass executors are synchronous callables that only record GPU commands without
 blocking or yielding. They must be lightweight and predictable.

 @warning Pass executors must NOT use coroutines or any async constructs.
          They are purely command recording functions.
 */
using PassExecutor = std::move_only_function<void(TaskExecutionContext&)>;

} // namespace oxygen::engine::asyncsim
