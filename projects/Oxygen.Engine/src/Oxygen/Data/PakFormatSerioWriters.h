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
  if (!data::pak::world::HasValidPerspectiveCameraValues(record)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return StoreRecord(writer, record);
}

//! Store the current packed camera record, rejecting invalid volumes.
inline auto Store(AnyWriter& writer,
  const data::pak::world::OrthographicCameraRecord& record) -> Result<void>
{
  if (!data::pak::world::HasValidOrthographicCameraValues(record)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return StoreRecord(writer, record);
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
  return StoreRecord(writer, record);
}

inline auto Store(AnyWriter& writer,
  const data::pak::world::ExposureCompensationKeyRecord& key) -> Result<void>
{
  if (!std::isfinite(key.metered_ev) || !std::isfinite(key.compensation_ev)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return StoreRecord(writer, key);
}

} // namespace oxygen::serio
