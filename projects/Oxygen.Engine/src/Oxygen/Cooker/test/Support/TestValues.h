//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <glm/vec3.hpp>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::cooker::test {

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
