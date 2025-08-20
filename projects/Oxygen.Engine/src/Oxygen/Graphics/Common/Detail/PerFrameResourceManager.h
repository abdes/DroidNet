//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <concepts>
#include <functional>
#include <mutex>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Constants.h>
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
class PerFrameResourceManager {
public:
  PerFrameResourceManager() = default;
  ~PerFrameResourceManager() = default;

  OXYGEN_MAKE_NON_COPYABLE(PerFrameResourceManager)
  OXYGEN_MAKE_NON_MOVABLE(PerFrameResourceManager)

  //! Registers a resource managed through a `std::shared_ptr` for deferred
  //! release.
  /*!
   @note This method can be used for resources that are released via their
   destructor, or can be used with a custom deleter. The custom deleter can
   help release the resource to an allocator, a shared pool, etc.
  */
  template <typename T>
  void RegisterDeferredRelease(std::shared_ptr<T> resource)
    requires HasReleaseMethod<T>
  {
    auto frame_idx = current_frame_slot_.load(std::memory_order_acquire);
    auto& frame_resources = deferred_releases_[frame_idx];
    {
      std::lock_guard<std::mutex> lock(deferred_mutexes_[frame_idx]);
      frame_resources.emplace_back([resource = std::move(resource)]() mutable {
        LogRelease(resource.get());
        resource->Release();
        resource.reset();
      });
    }
  }

  //! Registers a resource managed through a `std::shared_ptr` for deferred
  //! release.
  /*!
   @note This method can be used for resources that are released via their
   destructor, or can be used with a custom deleter. The custom deleter can
   help release the resource to an allocator, a shared pool, etc.
  */
  template <typename T>
  void RegisterDeferredRelease(std::shared_ptr<T> resource)
  {
    auto frame_idx = current_frame_slot_.load(std::memory_order_acquire);
    auto& frame_resources = deferred_releases_[frame_idx];
    {
      std::lock_guard<std::mutex> lock(deferred_mutexes_[frame_idx]);
      frame_resources.emplace_back([resource = std::move(resource)]() mutable {
        LogRelease(resource.get());
        resource.reset();
      });
    }
  }

  //! Registers a resource  that has a `Release()` method for deferred
  //! release. When the resource is finally released, the pointer is also set
  //! to `nullptr`.
  template <HasReleaseMethod T>
  void RegisterDeferredRelease(T* const resource) noexcept
    requires HasReleaseMethod<T>
  {
    auto frame_idx = current_frame_slot_.load(std::memory_order_acquire);
    auto& frame_resources = deferred_releases_[frame_idx];
    {
      std::lock_guard<std::mutex> lock(deferred_mutexes_[frame_idx]);
      frame_resources.emplace_back([resource]() mutable {
        if (resource) {
          LogRelease(resource);
          resource->Release();
        }
      });
    }
  }

  //! Enqueue an arbitrary action to run when the observed frame slot cycles.
  OXYGEN_GFX_API void RegisterDeferredAction(std::function<void()> action);

  //! Called at the beginning of a new frame to release resources from the
  //! last render of that same frame slot.
  OXYGEN_GFX_API void OnBeginFrame(frame::Slot frame_slot);

  //! Releases all deferred resources from all frames.
  OXYGEN_GFX_API void OnRendererShutdown();

  //! Process all deferred releases for all frames.
  OXYGEN_GFX_API void ProcessAllDeferredReleases();

private:
  //! Releases all deferred resources from the previous render of the frame.
  void ReleaseDeferredResources(frame::Slot frame_slot);

  //! Logs the release of a resource.
  template <typename T> static void LogRelease(const T* resource)
  {
#if !defined(NDEBUG)
    if (!resource)
      return;

    std::string_view type_name { "(no type info)" };
    std::string_view name { "(unnamed)" };

    if constexpr (HasGetTypeName<T>) {
      type_name = resource->GetTypeName();
    }
    if constexpr (HasGetNameMethod<T>) {
      name = resource->GetName();
    }

    DLOG_F(3, "Releasing {} resource: {}", type_name, name);
#endif
  }

  //! The current frame index.
  std::atomic<frame::Slot::UnderlyingType> current_frame_slot_ { 0 };

  //! The set of lambda functions that release the pending resources.
  std::vector<std::function<void()>>
    deferred_releases_[frame::kFramesInFlight.get()] {};

  //! Mutex per frame bucket to allow thread-safe registration from workers.
  std::mutex deferred_mutexes_[frame::kFramesInFlight.get()] {};
};

} // namespace oxygen::graphics::detail
