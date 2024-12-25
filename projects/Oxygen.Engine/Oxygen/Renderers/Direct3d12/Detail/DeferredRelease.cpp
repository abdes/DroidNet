//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Detail/DeferredRelease.h"

#include "Oxygen/Renderers/Direct3d12/Detail/dx12_utils.h"
#include "Oxygen/Renderers/Direct3d12/Detail/IDeferredReleaseController.h"
#include "Oxygen/Renderers/Direct3d12/Renderer.h"

using namespace oxygen::renderer::d3d12::detail;

void DeferredReleaseTracker::DeferRelease(IUnknown* resource) noexcept
{
  DCHECK_NOTNULL_F(resource);
  if (resource == nullptr) return;

  const auto renderer = renderer_.lock();
  if (!renderer) {
    LOG_F(WARNING, "DeferredRelease not initialized, renderer is not available, immediately releasing the resource");
    resource->Release();
    return;
  }

  {
    std::lock_guard lock{ mutex_ };
    deferred_releases_[GetRenderer().CurrentFrameIndex()].push_back(resource);
  }

  renderer->RegisterDeferredReleases(
    [this](const size_t frame_index)
    {
      ProcessDeferredReleases(frame_index);
    });
}

void DeferredReleaseTracker::ProcessDeferredReleases(const size_t frame_index)
{
  DCHECK_LE_F(frame_index, kFrameBufferCount);
  DLOG_F(1, "DeferredResourceReleaseTracker::ProcessDeferredReleases for frame index `{}`", frame_index);

  std::lock_guard lock{ mutex_ };
  auto& deferred_releases = deferred_releases_[frame_index];
  for (auto resource : deferred_releases)
  {
    ObjectRelease(resource);
  }
  deferred_releases.clear();
}
