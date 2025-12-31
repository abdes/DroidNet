//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Unique identifier for a content source (PAK file or loose cooked folder).
/*!
 A 128-bit GUID that uniquely identifies a content source. This is used to
 ensure that resources are correctly scoped and cached even when content sources
 are mounted/unmounted or reordered.
*/
struct SourceKey
  : public NamedType<std::array<uint8_t, 16>, struct SourceKeyTag,
      // clang-format off
      Comparable,
      Printable,
      Hashable> // clang-format on
{
  using Base = NamedType<std::array<uint8_t, 16>, struct SourceKeyTag,
    // clang-format off
    oxygen::Comparable,
    oxygen::Printable,
    oxygen::Hashable>; // clang-format on

  using Base::Base;

  //! Create a SourceKey from a C-style byte array.
  static auto FromBytes(const uint8_t (&bytes)[16]) -> SourceKey
  {
    std::array<uint8_t, 16> arr;
    std::copy(std::begin(bytes), std::end(bytes), arr.begin());
    return SourceKey(arr);
  }
};

//! String representation of SourceKey.
OXGN_DATA_NDAPI auto to_string(const SourceKey& key) -> std::string;

} // namespace oxygen::data

template <> struct std::hash<oxygen::data::SourceKey> {
  auto operator()(const oxygen::data::SourceKey& k) const noexcept
    -> std::size_t
  {
    size_t seed = 0;
    for (auto b : k.get()) {
      oxygen::HashCombine(seed, b);
    }
    return seed;
  }
};
