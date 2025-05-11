//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>

using oxygen::graphics::detail::PerFrameResourceManager;

void PerFrameResourceManager::OnBeginFrame(const uint32_t frame_index)
{
    current_frame_index_ = frame_index;
    ReleaseDeferredResources(frame_index);
}

void PerFrameResourceManager::OnRendererShutdown()
{
    DLOG_F(INFO, "Releasing all deferred resource for all frames...");
    for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
        ReleaseDeferredResources(i);
    }
}

void PerFrameResourceManager::ReleaseDeferredResources(const uint32_t frame_index)
{
    auto& frame_resources = deferred_releases_[frame_index];

#if !defined(NDEBUG)
    if (!frame_resources.empty()) {
        LOG_SCOPE_F(2, fmt::format("Deferred releases for frame [{}]", frame_index).c_str());
        DLOG_F(2, "{} objects to release", frame_resources.size());
    }
#endif // NDEBUG

    for (auto& release : frame_resources) {
        release();
    }
    frame_resources.clear();
}
