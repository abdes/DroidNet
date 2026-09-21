//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::serio {

//! Store the current packed camera record, rejecting invalid physical inputs.
inline auto Store(AnyWriter& writer,
  const data::pak::world::PerspectiveCameraRecord& record) -> Result<void>
{
  if (!std::isfinite(record.aperture_f) || record.aperture_f <= 0.0F
    || !std::isfinite(record.shutter_rate) || record.shutter_rate <= 0.0F
    || !std::isfinite(record.iso) || record.iso <= 0.0F) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  auto pack = writer.ScopedAlignment(1);
  CHECK_RESULT(writer.Write(record.node_index));
  CHECK_RESULT(writer.Write(record.fov_y));
  CHECK_RESULT(writer.Write(record.aspect_ratio));
  CHECK_RESULT(writer.Write(record.near_plane));
  CHECK_RESULT(writer.Write(record.far_plane));
  CHECK_RESULT(writer.Write(record.aperture_f));
  CHECK_RESULT(writer.Write(record.shutter_rate));
  CHECK_RESULT(writer.Write(record.iso));
  return {};
}

//! Store the current packed camera record, rejecting invalid physical inputs.
inline auto Store(AnyWriter& writer,
  const data::pak::world::OrthographicCameraRecord& record) -> Result<void>
{
  if (!std::isfinite(record.aperture_f) || record.aperture_f <= 0.0F
    || !std::isfinite(record.shutter_rate) || record.shutter_rate <= 0.0F
    || !std::isfinite(record.iso) || record.iso <= 0.0F) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  auto pack = writer.ScopedAlignment(1);
  CHECK_RESULT(writer.Write(record.node_index));
  CHECK_RESULT(writer.Write(record.left));
  CHECK_RESULT(writer.Write(record.right));
  CHECK_RESULT(writer.Write(record.bottom));
  CHECK_RESULT(writer.Write(record.top));
  CHECK_RESULT(writer.Write(record.near_plane));
  CHECK_RESULT(writer.Write(record.far_plane));
  CHECK_RESULT(writer.Write(record.aperture_f));
  CHECK_RESULT(writer.Write(record.shutter_rate));
  CHECK_RESULT(writer.Write(record.iso));
  return {};
}

//! Store only the current exposure prefix; its curve keys follow separately.
inline auto Store(AnyWriter& writer,
  const data::pak::world::PostProcessVolumeEnvironmentRecord& record)
  -> Result<void>
{
  const auto reserved_is_zero
    = [](const uint32_t value) -> bool { return value == 0U; };
  if (!data::pak::world::HasValidPostProcessVolumeValues(record)
    || record.exposure_extension_version
      != data::pak::world::kExposureExtensionVersion
    || record.curve_key_count > engine::kMaxExposureCompensationCurveKeys
    || record.header.record_size
      != sizeof(record)
        + (record.curve_key_count
          * sizeof(data::pak::world::ExposureCompensationKeyRecord))
    || !std::ranges::all_of(record.exposure_reserved, reserved_is_zero)
    || !std::ranges::all_of(record.curve_reserved, reserved_is_zero)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  auto pack = writer.ScopedAlignment(1);
  CHECK_RESULT(writer.Write(record.header.system_type));
  CHECK_RESULT(writer.Write(record.header.record_size));
  CHECK_RESULT(writer.Write(record.enabled));
  CHECK_RESULT(writer.Write(record.tone_mapper));
  CHECK_RESULT(writer.Write(record.exposure_mode));
  CHECK_RESULT(writer.Write(record.exposure_compensation_ev));
  CHECK_RESULT(writer.Write(record.auto_exposure_min_ev));
  CHECK_RESULT(writer.Write(record.auto_exposure_max_ev));
  CHECK_RESULT(writer.Write(record.auto_exposure_speed_up));
  CHECK_RESULT(writer.Write(record.auto_exposure_speed_down));
  CHECK_RESULT(writer.Write(record.bloom_intensity));
  CHECK_RESULT(writer.Write(record.bloom_threshold));
  CHECK_RESULT(writer.Write(record.saturation));
  CHECK_RESULT(writer.Write(record.contrast));
  CHECK_RESULT(writer.Write(record.vignette_intensity));
  CHECK_RESULT(writer.Write(record.exposure_enabled));
  CHECK_RESULT(writer.Write(record.exposure_key));
  CHECK_RESULT(writer.Write(record.manual_exposure_ev));
  CHECK_RESULT(writer.Write(record.auto_exposure_metering_mode));
  CHECK_RESULT(writer.Write(record.auto_exposure_low_percentile));
  CHECK_RESULT(writer.Write(record.auto_exposure_high_percentile));
  CHECK_RESULT(writer.Write(record.auto_exposure_min_log_luminance));
  CHECK_RESULT(writer.Write(record.auto_exposure_log_luminance_range));
  CHECK_RESULT(writer.Write(record.auto_exposure_target_luminance));
  CHECK_RESULT(writer.Write(record.auto_exposure_spot_meter_radius));
  CHECK_RESULT(writer.Write(record.display_gamma));
  CHECK_RESULT(writer.Write(record.exposure_extension_version));
  CHECK_RESULT(writer.Write(record.auto_exposure_black_influence));
  CHECK_RESULT(writer.Write(record.auto_exposure_transition_distance_ev));
  CHECK_RESULT(writer.Write(record.auto_exposure_metering_mask.get()));
  for (const auto value : record.exposure_reserved) {
    CHECK_RESULT(writer.Write(value));
  }
  CHECK_RESULT(writer.Write(record.curve_key_count));
  for (const auto value : record.curve_reserved) {
    CHECK_RESULT(writer.Write(value));
  }
  return {};
}

inline auto Store(AnyWriter& writer,
  const data::pak::world::ExposureCompensationKeyRecord& key) -> Result<void>
{
  if (!std::isfinite(key.metered_ev) || !std::isfinite(key.compensation_ev)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  auto pack = writer.ScopedAlignment(1);
  CHECK_RESULT(writer.Write(key.metered_ev));
  CHECK_RESULT(writer.Write(key.compensation_ev));
  return {};
}

} // namespace oxygen::serio
