//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <glm/vec3.hpp>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::cooker::test {

//! Whether import requests hash authored content by default when the
//! descriptor opts out: Release builds always hash, Debug builds honor the
//! opt-out. The single build-configuration branch lives here so tests share one
//! assertion line.
#ifdef NDEBUG
inline constexpr bool kContentHashingDefault = true;
#else
inline constexpr bool kContentHashingDefault = false;
#endif

//! Asset key with every byte set to `seed`; equal seeds give equal keys.
[[nodiscard]] inline auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  auto bytes = std::array<uint8_t, data::AssetKey::kSizeBytes> {};
  bytes.fill(seed);
  return data::AssetKey::FromBytes(bytes);
}

//! Expects each component of `actual` within `tolerance` of `expected`.
inline auto ExpectVec3Near(const glm::vec3& actual, const glm::vec3& expected,
  const float tolerance = 1e-4F) -> void
{
  EXPECT_NEAR(actual.x, expected.x, tolerance);
  EXPECT_NEAR(actual.y, expected.y, tolerance);
  EXPECT_NEAR(actual.z, expected.z, tolerance);
}

} // namespace oxygen::cooker::test
