//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <Oxygen/Config/PathFinderConfig.h>

namespace oxygen {

enum class ShadowQualityTier : std::uint8_t {
  kLow,
  kMedium,
  kHigh,
  kUltra,
};

enum class DirectionalShadowImplementationPolicy : std::uint8_t {
  kConventionalOnly,
  kVirtualShadowMap,
};

[[nodiscard]] constexpr auto to_string(
  const DirectionalShadowImplementationPolicy policy) noexcept
  -> std::string_view
{
  switch (policy) {
  case DirectionalShadowImplementationPolicy::kConventionalOnly:
    return "conventional";
  case DirectionalShadowImplementationPolicy::kVirtualShadowMap:
    return "vsm";
  default:
    return "unknown";
  }
}

struct RendererConfig {
  //! Immutable configuration for path resolution.
  PathFinderConfig path_finder_config;

  //! Upload queue key to use for staging/upload recording. Renderer will set
  //! this into the UploadPolicy passed to the UploadCoordinator. This field
  //! is required; do not default-initialize.
  std::string upload_queue_key;

  //! Maximum number of simultaneously prepared views the renderer keeps alive
  //! before evicting the least-recently-used entry. Default keeps legacy 8.
  std::size_t max_active_views { kDefaultMaxActiveViews };

  //! Renderer-owned shadow quality policy.
  //!
  //! This is not authored scene state. It selects runtime shadow budgets and
  //! quality behavior within the renderer.
  ShadowQualityTier shadow_quality_tier { ShadowQualityTier::kHigh };

  //! Renderer-owned directional shadow family policy.
  //!
  //! This is not authored scene state. It selects the directional shadow
  //! implementation family used by the renderer.
  DirectionalShadowImplementationPolicy directional_shadow_policy {
    DirectionalShadowImplementationPolicy::kConventionalOnly
  };

  //! Enable renderer-owned ImGui plumbing when supported by the active
  //! graphics backend.
  //!
  //! The application still owns the actual UI draw code, but the renderer owns
  //! the backend setup, frame lifecycle, and final composition plumbing.
  bool enable_imgui { false };

  //! Renderer-wide lighting/shadow allocation ceilings, including retained
  //! generations. These are admission limits, not preallocation targets.
  std::uint64_t lighting_allocation_limit_bytes { kDefaultLightingLimitBytes };
  std::uint64_t lighting_compact_index_limit_bytes {
    kDefaultCompactIndexLimitBytes,
  };
  //! Additional reserve below the driver's current segment budget.
  std::uint64_t lighting_driver_headroom_bytes { kDefaultDriverHeadroomBytes };

private:
  static constexpr std::uint64_t kDefaultLightingLimitBytes
    = 4ULL * 1024ULL * 1024ULL * 1024ULL;
  static constexpr std::uint64_t kDefaultCompactIndexLimitBytes
    = 128ULL * 1024ULL * 1024ULL;
  static constexpr std::uint64_t kDefaultDriverHeadroomBytes
    = 256ULL * 1024ULL * 1024ULL;
  static constexpr std::size_t kDefaultMaxActiveViews = 8;
};

} // namespace oxygen
