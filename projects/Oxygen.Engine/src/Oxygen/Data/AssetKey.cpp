//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <random>

#include <fmt/format.h>
#include <uuid.h> // stduuid: https://github.com/mariusbancila/stduuid

#include <Oxygen/Data/AssetKey.h>

using oxygen::data::AssetKey;

auto oxygen::data::GenerateAssetGuid() -> std::array<uint8_t, 16>
{
  std::random_device rd;
  auto seed_data = std::array<int, std::mt19937::state_size> {};
  std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
  std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
  std::mt19937 generator(seq);
  uuids::uuid_random_generator gen { generator };

  const auto id = gen();
  std::array<uint8_t, 16> arr;
  std::memcpy(arr.data(), id.as_bytes().data(), 16);
  return arr;
}

auto oxygen::data::to_string(oxygen::data::AssetKey value) noexcept
  -> std::string
{
  std::string result;
  result.reserve(36); // UUID string representation length
  for (size_t i = 0; i < value.guid.size(); ++i) {
    if (i > 0 && (i == 4 || i == 6 || i == 8 || i == 10)) {
      result += '-';
    }
    result += fmt::format("{:02x}", value.guid[i]);
  }
  return result;
}
