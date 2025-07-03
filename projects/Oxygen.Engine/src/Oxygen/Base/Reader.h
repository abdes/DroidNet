//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <bit>
#include <functional> // for std::reference_wrapper
#include <string>
#include <vector>

#include <Oxygen/Base/AlignmentGuard.h>
#include <Oxygen/Base/Endian.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Stream.h>

namespace oxygen::serio {

template <Stream S> class Reader : public Packer {
public:
  explicit Reader(S& stream) noexcept
    : stream_(stream)
  {
  }

  [[nodiscard]] auto ScopedAlignement(uint8_t alignment) noexcept(false)
    -> ::oxygen::serio::AlignmentGuard
  {
    return AlignmentGuard(*this, alignment);
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  [[nodiscard]] auto read() noexcept -> Result<T>
  {
    if constexpr (!std::is_floating_point_v<T>) {
      static_assert(std::has_unique_object_representations_v<T>,
        "Type may have platform-dependent representation");
    }
    static_assert(!std::is_floating_point_v<T>
        || (sizeof(float) == 4
          // NOLINTNEXTLINE(*-magic-numbers)
          && sizeof(double) == 8),
      "Platform must use IEEE-754 floating point representation");

    if constexpr (sizeof(T) > 1) {
      CHECK_RESULT(align_to(alignof(T)));
    }

    T value;
    CHECK_RESULT(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      stream_.get().read(reinterpret_cast<std::byte*>(&value), sizeof(T)));

    if (!IsLittleEndian() && sizeof(T) > 1) {
      value = ByteSwap(value);
    }
    return value;
  }

  [[nodiscard]] auto read_string() noexcept -> Result<std::string>
  {
    try {
      // Align for length field
      CHECK_RESULT(align_to(alignof(limits::SequenceSizeType)));

      auto length = limits::SequenceSizeType {};
      CHECK_RESULT(read_string_length(length));

      std::string str(length, '\0');
      CHECK_RESULT(
        stream_.get().read(reinterpret_cast<std::byte*>(str.data()), length));

      // Skip padding to maintain alignment
      CHECK_RESULT(align_to(alignof(uint32_t)));

      return str;
    } catch (const std::exception& /*ex*/) {
      return std::make_error_code(std::errc::io_error);
    }
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  [[nodiscard]] auto read_array() noexcept -> Result<std::vector<T>>
  {
    try {
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
        if (!item_result) {
          return item_result.error();
        }
        array.push_back(item_result.value());
      }

      // Skip padding to maintain alignment
      CHECK_RESULT(align_to(alignof(uint32_t)));

      return array;
    } catch (const std::exception& /*ex*/) {
      return std::make_error_code(std::errc::io_error);
    }
  }

  [[nodiscard]] auto read_blob(size_t size) noexcept
    -> Result<std::vector<std::byte>>
  {
    try {
      if (size == 0) {
        return std::vector<std::byte> {};
      }
      std::vector<std::byte> buffer(size);
      auto result = stream_.get().read(buffer.data(), size);
      if (!result) {
        return result.error();
      }
      return buffer;
    } catch (const std::exception& /*ex*/) {
      return std::make_error_code(std::errc::io_error);
    }
  }

  [[nodiscard]] auto read_blob_to(std::span<std::byte> buffer) noexcept
    -> Result<void>
  {
    return stream_.get().read(buffer.data(), buffer.size());
  }

  [[nodiscard]] auto position() noexcept -> Result<size_t>
  {
    return stream_.get().position();
  }

  [[nodiscard]] auto align_to(size_t alignment) noexcept -> Result<void>
  {
    if (!alignment_.empty() && !alignment_.top() == 0) {
      // override alignment with the top of the stack
      alignment = alignment_.top();
    }

    try {
      const auto current_pos = stream_.get().position();
      if (!current_pos) {
        return current_pos.error();
      }

      const size_t padding
        = (alignment - (current_pos.value() % alignment)) % alignment;
      if (padding > 0) {
        return stream_.get().forward(padding);
      }
      return {};
    } catch (const std::exception& /*ex*/) {
      return std::make_error_code(std::errc::io_error);
    }
  }

  [[nodiscard]] auto forward(size_t num_bytes) noexcept -> Result<void>
  {
    return stream_.get().forward(num_bytes);
  }

private:
  [[nodiscard]] auto read_string_length(
    limits::SequenceSizeType& length) noexcept -> Result<void>
  {
    return read_length(length, limits::kMaxStringLength);
  }

  [[nodiscard]] auto read_array_length(
    limits::SequenceSizeType& length) noexcept -> Result<void>
  {
    return read_length(length, limits::kMaxArrayLength);
  }

  [[nodiscard]] auto read_length(limits::SequenceSizeType& length,
    limits::SequenceSizeType max_length) noexcept -> Result<void>
  {
    try {
      const auto length_result = read<limits::SequenceSizeType>();
      if (!length_result) {
        return length_result.error();
      }

      length = length_result.value();
      if (length > max_length) {
        length = 0;
        return std::make_error_code(std::errc::value_too_large);
      }

      return {};
    } catch (const std::exception& /*ex*/) {
      return std::make_error_code(std::errc::io_error);
    }
  }

  std::reference_wrapper<S> stream_;
};
} // namespace oxygen::serio
