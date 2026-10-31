//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>

#include <Oxygen/Data/LightValidation.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::serio {

//=== Scene: Nodes & Components (v2/v3) ===----------------------------------//

inline auto Load(AnyReader& reader,
  data::pak::world::LocalFogVolumeRecord& record) -> Result<void>
{
  CHECK_RESULT(LoadRecord(reader, record));
  if (!data::pak::world::HasValidLocalFogVolumeValues(record)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::PerspectiveCameraRecord& record) -> Result<void>
{
  CHECK_RESULT(LoadRecord(reader, record));
  if (!data::pak::world::HasValidPerspectiveCameraValues(record)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::OrthographicCameraRecord& record) -> Result<void>
{
  CHECK_RESULT(LoadRecord(reader, record));
  if (!data::pak::world::HasValidOrthographicCameraValues(record)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::scripting::ScriptParamRecord& record) -> Result<void>
{
  CHECK_RESULT(LoadRecord(reader, record));
  if (!data::pak::scripting::HasValidScriptParamValues(record)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::scripting::ScriptParamOverrideRecord& record) -> Result<void>
{
  CHECK_RESULT(LoadRecord(reader, record));
  if (!data::pak::scripting::HasValidScriptParamValues(record.param)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return {};
}

//=== Scene: Environment (v3+) ===------------------------------------------//

inline auto Load(AnyReader& reader,
  data::pak::world::PostProcessVolumeEnvironmentRecord& r) -> Result<void>
{
  CHECK_RESULT(LoadRecord(reader, r));
  const auto reserved_is_zero
    = [](const uint32_t value) -> bool { return value == 0U; };
  if (!data::pak::world::HasValidPostProcessVolumeValues(r)
    || r.exposure_extension_version
      != data::pak::world::kExposureExtensionVersion
    || r.curve_key_count > engine::kMaxExposureCompensationCurveKeys
    || !std::ranges::all_of(r.exposure_reserved, reserved_is_zero)
    || !std::ranges::all_of(r.curve_reserved, reserved_is_zero)
    || r.header.record_size
      != sizeof(r)
        + (r.curve_key_count
          * sizeof(data::pak::world::ExposureCompensationKeyRecord))) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }

  return {};
}

inline auto Load(AnyReader& reader,
  data::pak::world::ExposureCompensationKeyRecord& key) -> Result<void>
{
  CHECK_RESULT(LoadRecord(reader, key));
  if (!std::isfinite(key.metered_ev) || !std::isfinite(key.compensation_ev)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return {};
}

//=== Scene: Lights (v3+) ===------------------------------------------------//

inline auto Load(AnyReader& reader, data::pak::world::DirectionalLightRecord& r)
  -> Result<void>
{
  CHECK_RESULT(LoadRecord(reader, r));
  if (!data::IsValidLightRecord(r)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return {};
}

inline auto Load(AnyReader& reader, data::pak::world::PointLightRecord& r)
  -> Result<void>
{
  CHECK_RESULT(LoadRecord(reader, r));
  if (!data::IsValidLightRecord(r)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return {};
}

inline auto Load(AnyReader& reader, data::pak::world::SpotLightRecord& r)
  -> Result<void>
{
  CHECK_RESULT(LoadRecord(reader, r));
  if (!data::IsValidLightRecord(r)) {
    return ::oxygen::Err(std::errc::invalid_argument);
  }
  return {};
}

} // namespace oxygen::serio
