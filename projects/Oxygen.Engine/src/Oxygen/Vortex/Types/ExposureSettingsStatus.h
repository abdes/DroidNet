//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Scene/ExposureSettings.h>

namespace oxygen::vortex {

//! Residency of the mask in the most recent numerically valid exposure request.
enum class ExposureMaskStatus : std::uint8_t {
  kAbsent,
  kPending,
  kReady,
  kFailed
};

[[nodiscard]] inline auto to_string(const ExposureMaskStatus status) noexcept
  -> std::string_view
{
  switch (status) {
  case ExposureMaskStatus::kAbsent:
    return "Absent";
  case ExposureMaskStatus::kPending:
    return "Pending";
  case ExposureMaskStatus::kReady:
    return "Ready";
  case ExposureMaskStatus::kFailed:
    return "Failed";
  }
  return "__NotSupported__";
}

//! Owned diagnostic snapshot; contains authored settings, never GPU Auto gain.
struct ExposureSettingsStatus {
  bool uses_view_override { false };
  bool shared_source { false };
  std::optional<scene::ExposureSettings> active_settings;
  std::optional<float> active_camera_ev;
  std::uint64_t revision { 0U };
  std::optional<scene::ExposureSettingsError> settings_error;
  ExposureMaskStatus mask_status { ExposureMaskStatus::kAbsent };
  content::ResourceKey requested_mask {};
  std::string mask_error;
  //! Last qualified asynchronous input status, absent when not observed.
  std::optional<bool> metering_input_failed;
  std::uint64_t observed_frame { 0U };
};

} // namespace oxygen::vortex
