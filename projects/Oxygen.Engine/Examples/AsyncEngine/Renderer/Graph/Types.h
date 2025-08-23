//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Renderer/Types/View.h>

namespace oxygen::examples::asyncsim {

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
  PerView //!< Passes that run independently for each view
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

//! Strong-typed handle for resources
// clang-format off
using ResourceHandle = oxygen::NamedType<
  uint32_t,
  struct ResourceHandleTag,
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

//! Defines a single view of the scene with its own camera, viewport, and target
//! surface
/*!
 Keep this lightweight for efficient capture semantics. For heavier data,
 store indices and fetch via TaskExecutionContext.
 */
struct ViewContext {
  ViewId view_id { 0 }; //!< Unique identifier for this view
  uint32_t surface_index { 0 }; //!< Index of the target surface
  std::shared_ptr<void> surface; //!< Target surface (window/render target) -
                                 //!< placeholder for graphics::Surface
  oxygen::engine::View camera; //!< View-specific camera matrices and parameters
  std::string view_name; //!< Human-readable name for this view

  // Viewport definition
  struct Viewport {
    float x { 0.0f }, y { 0.0f }, width { 1920.0f }, height { 1080.0f };
    float min_depth { 0.0f }, max_depth { 1.0f };
  } viewport;

  ViewContext()
    : view_id { 0 }
    , surface_index { 0 }
    , surface(nullptr)
    , camera(CreateDefaultView())
    , view_name("default")
  {
  }

  ViewContext(ViewId id, uint32_t surf_idx, std::shared_ptr<void> surf,
    const oxygen::engine::View& cam, std::string name)
    : view_id(id)
    , surface_index(surf_idx)
    , surface(std::move(surf))
    , camera(cam)
    , view_name(std::move(name))
  {
  }

private:
  static auto CreateDefaultView() -> oxygen::engine::View
  {
    oxygen::engine::View::Params p;
    p.view = glm::mat4(1.0f);
    p.proj = glm::mat4(1.0f);
    p.viewport = glm::ivec4(0, 0, 1920, 1080); // Default viewport
    p.reverse_z = false;
    return oxygen::engine::View(p);
  }
};

//! Contains shared frame data and all view definitions
struct FrameContext {
  uint64_t frame_index { 0 }; //!< Current frame number
  std::shared_ptr<void>
    scene_data; //!< Shared scene geometry/materials - placeholder
  std::vector<ViewContext> views; //!< All views to render this frame

  FrameContext() = default;

  explicit FrameContext(uint64_t frame_idx)
    : frame_index(frame_idx)
  {
  }
};

} // namespace oxygen::examples::asyncsim
