//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
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

private:
  static constexpr std::size_t kDefaultMaxActiveViews = 8;
};

} // namespace oxygen
