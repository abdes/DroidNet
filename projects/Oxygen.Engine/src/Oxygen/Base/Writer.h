//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <bit>
#include <functional> // for std::reference_wrapper
#include <span>
#include <stack>
#include <string_view>

#include <Oxygen/Base/AlignmentGuard.h>
#include <Oxygen/Base/Endian.h>
#include <Oxygen/Base/Stream.h>

namespace oxygen::serio {

template <Stream S> class Writer : public Packer {
public:
  explicit Writer(S& stream) noexcept
    : stream_(stream)
  {
  }

  [[nodiscard]] auto ScopedAlignment(uint16_t alignment) noexcept(false)
    -> AlignmentGuard
  {
    return AlignmentGuard(*this, alignment);
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  [[nodiscard]] auto write(const T& value) noexcept -> Result<void>
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

    return write_raw(value);
  }

  [[nodiscard]] auto write_string(std::string_view str) noexcept -> Result<void>
  {
    if (str.length() > limits::kMaxArrayLength) {
      return std::make_error_code(std::errc::value_too_large);
    }

    // Align for length field
    CHECK_RESULT(align_to(alignof(limits::SequenceSizeType)));

    // Write length (with endianness handling)
    auto length = static_cast<limits::SequenceSizeType>(str.length());
    if (!IsLittleEndian()) {
      length = ByteSwap(length);
    }

    // Write string length / data
    CHECK_RESULT(write_raw(length));
    CHECK_RESULT(stream_.get().write(
      reinterpret_cast<const std::byte*>(str.data()), str.length()));

    // Align to next boundary
    return align_to(alignof(uint32_t));
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  [[nodiscard]] auto write_array(std::span<const T> array) noexcept
    -> Result<void>
  {
    if (array.size() > limits::kMaxArrayLength) {
      return std::make_error_code(std::errc::message_size);
    }

    // Align for length field
    CHECK_RESULT(align_to(alignof(limits::SequenceSizeType)));

    // Write array length
    auto length = static_cast<limits::SequenceSizeType>(array.size());
    if (!IsLittleEndian()) {
      length = ByteSwap(length);
    }

    if (const auto result = write_raw(length); !result) {
      return result;
    }

    // Align for array elements if needed
    if constexpr (sizeof(T) > 1) {
      CHECK_RESULT(align_to(alignof(T)));
    }

    // Write array data
    for (const auto& item : array) {
      CHECK_RESULT(write_raw(item));
    }

    // Align to next boundary
    return align_to(alignof(uint32_t));
  }

  [[nodiscard]] auto write_blob(std::span<const std::byte> blob) noexcept
    -> Result<void>
  {
    return stream_.get().write(blob.data(), blob.size());
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
        std::array<std::byte, kMaxAlignment> zeros { std::byte { 0 } };
        return stream_.get().write(zeros.data(), padding);
      }
      return {};
    } catch (const std::exception& /*ex*/) {
      return std::make_error_code(std::errc::io_error);
    }
  }

  [[nodiscard]] auto position() const noexcept -> Result<size_t>
  {
    return stream_.get().position();
  }

  auto flush() noexcept -> Result<void> { return stream_.get().flush(); }

private:
  template <typename T>
    requires std::is_trivially_copyable_v<T>
  [[nodiscard]] auto write_raw(const T& value) noexcept -> Result<void>
  {
    T temp = value;
    if (!IsLittleEndian() && sizeof(T) > 1) {
      temp = ByteSwap(temp);
    }
    return stream_.get().write(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<const std::byte*>(&temp), sizeof(T));
  }

  std::reference_wrapper<S> stream_;
};

} // namespace oxygen::serio
