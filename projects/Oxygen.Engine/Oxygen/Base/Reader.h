//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional> // for std::reference_wrapper
#include <string>
#include <vector>

#include "Oxygen/Base/Endian.h"
#include "Oxygen/Base/Result.h"
#include "Oxygen/Base/Stream.h"

namespace oxygen::serio {

template <Stream S>
class Reader
{
 public:
  explicit Reader(S& stream) noexcept
    : stream_(stream)
  {
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  [[nodiscard]] auto read() noexcept -> Result<T>
  {
    if constexpr (!std::is_floating_point_v<T>) {
      static_assert(std::has_unique_object_representations_v<T>,
        "Type may have platform-dependent representation");
    }
    static_assert(!std::is_floating_point_v<T> || (sizeof(float) == 4 && sizeof(double) == 8),
      "Platform must use IEEE-754 floating point representation");

    if constexpr (sizeof(T) > 1) {
      CHECK_RESULT(align_to(alignof(T)));
    }

    T value;
    CHECK_RESULT(stream_.get().read(reinterpret_cast<char*>(&value), sizeof(T)));

    if (!IsLittleEndian() && sizeof(T) > 1) {
      value = ByteSwap(value);
    }
    return value;
  }

  [[nodiscard]] auto read_string() noexcept -> Result<std::string>
  {
    // Align for length field
    CHECK_RESULT(align_to(alignof(limits::SequenceSizeType)));

    auto length = limits::SequenceSizeType {};
    CHECK_RESULT(read_string_length(length));

    std::string str(length, '\0');
    CHECK_RESULT(stream_.get().read(str.data(), length));

    // Skip padding to maintain alignment
    CHECK_RESULT(align_to(alignof(uint32_t)));

    return str;
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  [[nodiscard]] auto read_array() noexcept -> Result<std::vector<T>>
  {
    // Align for length field
    CHECK_RESULT(align_to(alignof(limits::SequenceSizeType)));

    auto length = limits::SequenceSizeType {};
    CHECK_RESULT(read_array_length(length));

    // Align for array elements if needed
    if constexpr (sizeof(T) > 1) {
      CHECK_RESULT(align_to(alignof(T)));
    }

    std::vector<T> array;
    array.reserve(length);

    for (size_t i = 0; i < length; ++i) {
      auto item_result = read<T>();
      if (!item_result)
        return item_result.error();
      array.push_back(item_result.value());
    }

    // Skip padding to maintain alignment
    CHECK_RESULT(align_to(alignof(uint32_t)));

    return array;
  }

 private:
  [[nodiscard]] auto align_to(size_t alignment) noexcept -> Result<void>
  {
    const auto current_pos = stream_.get().position();
    if (!current_pos)
      return current_pos.error();

    const size_t padding = (alignment - (current_pos.value() % alignment)) % alignment;
    if (padding > 0) {
      char discard[32];
      return stream_.get().read(discard, padding);
    }
    return {};
  }

  [[nodiscard]] auto read_string_length(limits::SequenceSizeType& length) noexcept -> Result<void>
  {
    return read_length(length, limits::kMaxStringLength);
  }

  [[nodiscard]] auto read_array_length(limits::SequenceSizeType& length) noexcept -> Result<void>
  {
    return read_length(length, limits::kMaxArrayLength);
  }

  [[nodiscard]] auto read_length(limits::SequenceSizeType& length, limits::SequenceSizeType max_length) noexcept -> Result<void>
  {
    const auto length_result = read<limits::SequenceSizeType>();
    if (!length_result)
      return length_result.error();

    length = length_result.value();
    if (length > max_length) {
      length = 0;
      return std::make_error_code(std::errc::value_too_large);
    }

    return {};
  }

  std::reference_wrapper<S> stream_;
};
} // namespace oxygen::serio
