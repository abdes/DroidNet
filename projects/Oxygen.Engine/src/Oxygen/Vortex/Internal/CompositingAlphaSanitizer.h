//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <unordered_set>

namespace oxygen::vortex::internal::detail {

struct SanitizedAlphaResult {
  float alpha { 1.0F };
  bool log_invalid_alpha { false };
  bool log_clamped_alpha { false };
};

[[nodiscard]] inline auto SanitizeCompositingAlphaValue(
  const float alpha) noexcept -> float
{
  if (!std::isfinite(alpha)) {
    return 1.0F;
  }
  return std::clamp(alpha, 0.0F, 1.0F);
}

class AlphaWarningLimiter {
public:
  [[nodiscard]] auto ShouldLogInvalidAlpha(std::string_view debug_name) -> bool
  {
    return invalid_alpha_keys_.emplace(MakeKey(debug_name)).second;
  }

  [[nodiscard]] auto ShouldLogClampedAlpha(std::string_view debug_name) -> bool
  {
    return clamped_alpha_keys_.emplace(MakeKey(debug_name)).second;
  }

private:
  [[nodiscard]] static auto MakeKey(std::string_view debug_name) -> std::string
  {
    return debug_name.empty() ? std::string { "CompositingPass" }
                              : std::string { debug_name };
  }

  std::unordered_set<std::string> invalid_alpha_keys_ {};
  std::unordered_set<std::string> clamped_alpha_keys_ {};
};

[[nodiscard]] inline auto SanitizeCompositingAlpha(std::string_view debug_name,
  const float alpha, AlphaWarningLimiter& limiter) -> SanitizedAlphaResult
{
  const float sanitized_alpha = SanitizeCompositingAlphaValue(alpha);
  if (!std::isfinite(alpha)) {
    return SanitizedAlphaResult {
      .alpha = sanitized_alpha,
      .log_invalid_alpha = limiter.ShouldLogInvalidAlpha(debug_name),
      .log_clamped_alpha = false,
    };
  }
  if (sanitized_alpha != alpha) {
    return SanitizedAlphaResult {
      .alpha = sanitized_alpha,
      .log_invalid_alpha = false,
      .log_clamped_alpha = limiter.ShouldLogClampedAlpha(debug_name),
    };
  }

  return SanitizedAlphaResult { .alpha = sanitized_alpha };
}

} // namespace oxygen::vortex::internal::detail
