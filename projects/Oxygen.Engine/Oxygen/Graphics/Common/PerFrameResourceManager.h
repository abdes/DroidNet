//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <vector>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Macros.h"
#include "Oxygen/Graphics/Common/ObjectRelease.h"
#include "Oxygen/Graphics/Common/Types.h"

namespace oxygen::graphics {

//! Tracks resources allocated during the rendering of a frame and releases
//! them when no longer used by the GPU (i.e. at the beginning of the new
//! render for that same frame index).
class PerFrameResourceManager
{
 public:
  PerFrameResourceManager() = default;
  ~PerFrameResourceManager() = default;

  OXYGEN_MAKE_NON_COPYABLE(PerFrameResourceManager);
  OXYGEN_MAKE_NON_MOVEABLE(PerFrameResourceManager);

  //! Registers a resource managed through a `std::shared_ptr` for deferred
  //! release.
  /*!
   \note This method can be used for resources that are released via their
   destructor, or can be used with a custom deleter. The custom deleter can
   help release the resource to an allocator, a shared pool, etc.
  */
  template <typename T>
  void RegisterDeferredRelease(std::shared_ptr<T> resource)
    requires HasReleaseMethod<T>
  {
    auto& frame_resources = deferred_releases_[current_frame_index_];
    frame_resources.emplace_back(
      [resource = std::move(resource)]() mutable {
        resource->Release();
        resource.reset();
      });
  }

  //! Registers a resource managed through a `std::shared_ptr` for deferred
  //! release.
  /*!
   \note This method can be used for resources that are released via their
   destructor, or can be used with a custom deleter. The custom deleter can
   help release the resource to an allocator, a shared pool, etc.
  */
  template <typename T>
  void RegisterDeferredRelease(std::shared_ptr<T> resource)
  {
    auto& frame_resources = deferred_releases_[current_frame_index_];
    frame_resources.emplace_back(
      [resource = std::move(resource)]() mutable {
        resource.reset();
      });
  }

  //! Registers a resource  that has a `Release()` method for deferred
  //! release. When the resource is finally released, the pointer is also set
  //! to `nullptr`.
  template <HasReleaseMethod T>
  void RegisterDeferredRelease(T*& resource) noexcept
    requires HasReleaseMethod<T>
  {
    auto& frame_resources = deferred_releases_[current_frame_index_];
    frame_resources.emplace_back(
      [resource]() mutable {
        if (resource) {
          resource->Release();
          resource = nullptr;
        }
      });
  }

 private:
  // Declare MixinDeferredRelease as a friend so it can call OnBeginFrame(),
  // ReleaseDeferredResources() and ReleaseAllDeferredResources(). Other than
  // that, we do not want anyone else to call these methods and interfere with
  // the coordination of deferred resource management with the rendering state
  // transitions.
  template <typename Base>
  friend class MixinDeferredRelease;

  //! Called at the beginning of a new frame to release resources from the
  //! lats render of that same frame index.
  void OnBeginFrame(const uint32_t frame_index)
  {
    current_frame_index_ = frame_index;
    ReleaseDeferredResources(frame_index);
  }

  //! Releases all deferred resources from the previous render of the frame.
  void ReleaseDeferredResources(const uint32_t frame_index)
  {
    auto& frame_resources = deferred_releases_[frame_index];
    DLOG_F(2, "{} deferred resources from previous render of frame[{}] to release", frame_resources.size(), frame_index);
    for (auto& release : frame_resources) {
      release();
    }
    frame_resources.clear();
  }

  //! Releases all deferred resources from all frames. Typically called when
  //! the renderer is shutting down.
  void ReleaseAllDeferredResources()
  {
    DLOG_F(INFO, "Releasing all deferred resource");
    for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
      ReleaseDeferredResources(i);
    }
  }

  //! The current frame index.
  uint32_t current_frame_index_ { 0 };

  //! The set of lambda functions that release the pending resources.
  std::vector<std::function<void()>> deferred_releases_[kFrameBufferCount] {};
};

} // namespace oxygen
