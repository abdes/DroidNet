//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <Oxygen/Base/Sha256.h>

namespace oxygen::content::import {

//! Streams format-owned source layout values into an endian-stable witness.
class SourceLayoutHash final {
public:
  explicit SourceLayoutHash(const std::string_view domain)
  {
    hash_.Update(std::as_bytes(std::span(domain)));
  }

  auto Add(const uint32_t value) -> void { AddWord(value); }
  auto Add(const uint64_t value) -> void { AddWord(value); }
  auto AddFloat(const float value) -> void
  {
    Add(std::bit_cast<uint32_t>(value));
  }
  auto AddFloat(const double value) -> void
  {
    Add(std::bit_cast<uint64_t>(value));
  }
  auto AddCount(const size_t value) -> void
  {
    Add(static_cast<uint64_t>(value));
  }
  [[nodiscard]] auto Finish() -> base::Sha256Digest { return hash_.Finalize(); }

private:
  template <std::unsigned_integral T> auto AddWord(const T value) -> void
  {
    std::array<std::byte, sizeof(T)> bytes {};
    constexpr auto kBitsPerByte = 8U;
    constexpr T kByteMask = 0xffU;
    for (size_t index = 0; index < bytes.size(); ++index) {
      bytes.at(index)
        = static_cast<std::byte>((value >> (index * kBitsPerByte)) & kByteMask);
    }
    hash_.Update(bytes);
  }

  base::Sha256 hash_;
};

} // namespace oxygen::content::import
