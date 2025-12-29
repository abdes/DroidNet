//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics::detail {

//! Concept that checks if a type has a GetName method that returns something
//! convertible to std::string_view
template <typename T>
concept HasGetNameMethod = requires(T t) {
  { t.GetName() } -> std::convertible_to<std::string_view>;
};

//! Concept that checks if a type has an instance GetTypeName method that
//! returns something convertible to std::string_view
template <typename T>
concept HasGetTypeName = requires(T t) {
  { t.GetTypeName() } -> std::convertible_to<std::string_view>;
};

//! Tracks resources allocated during the rendering of a frame and releases
//! them when no longer used by the GPU (i.e., at the beginning of the new
//! render for that same frame index).
class DeferredReclaimer {

public:
  OXGN_GFX_API DeferredReclaimer();
  OXGN_GFX_API ~DeferredReclaimer();

  OXYGEN_MAKE_NON_COPYABLE(DeferredReclaimer)
  OXYGEN_MAKE_NON_MOVABLE(DeferredReclaimer)

  //! Registers a resource managed through a `std::shared_ptr` for deferred
  //! release.
  /*!
   @note This method can be used for resources that are released via their
   destructor, or can be used with a custom deleter. The custom deleter can
   help release the resource to an allocator, a shared pool, etc.
  */
  template <typename T>
  auto RegisterDeferredRelease(std::shared_ptr<T> resource) -> void
    requires HasReleaseMethod<T>
  {
    RegisterDeferredAction([resource = std::move(resource)]() mutable {
      LogRelease(resource.get());
      resource->Release();
      resource.reset();
    });
  }

  //! Registers a resource managed through a `std::shared_ptr` for deferred
  //! release.
  /*!
   @note This method can be used for resources that are released via their
   destructor, or can be used with a custom deleter. The custom deleter can
   help release the resource to an allocator, a shared pool, etc.
  */
  template <typename T>
  auto RegisterDeferredRelease(std::shared_ptr<T> resource) -> void
  {
    RegisterDeferredAction([resource = std::move(resource)]() mutable {
      LogRelease(resource.get());
      resource.reset();
    });
  }

  //! Registers a resource  that has a `Release()` method for deferred
  //! release. When the resource is finally released, the pointer is also set
  //! to `nullptr`.
  template <HasReleaseMethod T>
  auto RegisterDeferredRelease(T* const resource) noexcept -> void
    requires HasReleaseMethod<T>
  {
    RegisterDeferredAction([resource]() mutable {
      if (resource) {
        LogRelease(resource);
        resource->Release();
      }
    });
  }

  //! Enqueue an arbitrary action to run when the observed frame slot cycles.
  OXGN_GFX_API auto RegisterDeferredAction(std::function<void()> action)
    -> void;

  //! Called at the beginning of a new frame to release resources from the
  //! last render of that same frame slot.
  OXGN_GFX_API auto OnBeginFrame(frame::Slot frame_slot) -> void;

  //! Releases all deferred resources from all frames.
  OXGN_GFX_API auto OnRendererShutdown() -> void;

  //! Process all deferred releases for all frames.
  OXGN_GFX_API auto ProcessAllDeferredReleases() -> void;

private:
  //! Releases all deferred resources from the previous render of the frame.
  auto ReleaseDeferredResources(frame::Slot frame_slot) -> void;

  // Private implementation (PIMPL) — keeps internal state and heavy includes
  // out of the public header. The concrete implementation lives in the .cpp
  // and may depend on internal headers such as CommandListPool.
  struct Impl;
  std::unique_ptr<Impl> impl_;

  //! Logs the release of a resource.
  template <typename T>
  static auto LogRelease([[maybe_unused]] const T* resource) -> void
  {
#if !defined(NDEBUG)
    if (!resource) {
      return;
    }

    std::string_view type_name { "(no type info)" };
    std::string_view name { "(unnamed)" };

    if constexpr (HasGetTypeName<T>) {
      type_name = resource->GetTypeNamePretty();
    }
    if constexpr (HasGetNameMethod<T>) {
      name = resource->GetName();
    }

    DLOG_F(3, "Releasing resource, type_name={}, name={}", type_name, name);
#endif
  }

  //! The header intentionally hides the per-frame storage so it does not
  //! require any internal headers. Implementation details are in Impl.
};

} // namespace oxygen::graphics::detail
