//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <ranges>
#include <span>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::content::testing {

class MockStream {
public:
  explicit MockStream() = default;

  [[nodiscard]] auto Write(const std::byte* data, const size_t size) noexcept
    -> Result<void>
  {
    try {
      if (force_fail_) {
        return std::make_error_code(std::errc::io_error);
      }

      using difference_type = std::vector<std::byte>::difference_type;
      if (pos_
          > static_cast<size_t>(std::numeric_limits<difference_type>::max())
        || size
          > static_cast<size_t>(std::numeric_limits<difference_type>::max())) {
        return std::make_error_code(std::errc::value_too_large);
      }

      if (pos_ + size > data_.size()) {
        data_.resize(pos_ + size);
      }

      std::span<const std::byte> input_data { data, size };
      auto dest = std::ranges::subrange(
        data_.begin() + static_cast<difference_type>(pos_),
        data_.begin() + static_cast<difference_type>(pos_ + size));
      std::ranges::copy(input_data, dest.begin());
      pos_ += size;
      return {};
    } catch (const std::exception&) {
      return std::make_error_code(std::errc::io_error);
    }
  }

  [[nodiscard]] auto Write(const std::span<const std::byte> data) noexcept
    -> Result<void>
  {
    return Write(data.data(), data.size());
  }

  [[nodiscard]] auto Read(std::byte* data, const size_t size) noexcept
    -> Result<void>
  {
    if (force_fail_) {
      return std::make_error_code(std::errc::io_error);
    }

    using difference_type = std::vector<std::byte>::difference_type;
    if (pos_ > static_cast<size_t>(std::numeric_limits<difference_type>::max())
      || size
        > static_cast<size_t>(std::numeric_limits<difference_type>::max())) {
      return std::make_error_code(std::errc::value_too_large);
    }

    if (pos_ + size > data_.size()) {
      return std::make_error_code(std::errc::no_buffer_space);
    }

    auto source = std::ranges::subrange(
      data_.begin() + static_cast<difference_type>(pos_),
      data_.begin() + static_cast<difference_type>(pos_ + size));
    std::span<std::byte> output_data { data, size };

    std::ranges::copy(source, output_data.begin());
    pos_ += size;
    return {};
  }

  [[nodiscard]] auto Flush() noexcept -> Result<void>
  {
    if (force_fail_) {
      return std::make_error_code(std::errc::io_error);
    }
    return {};
  }

  [[nodiscard]] auto Position() const noexcept -> Result<size_t>
  {
    if (force_fail_) {
      return std::make_error_code(std::errc::io_error);
    }
    return pos_;
  }

  [[nodiscard]] auto Seek(const size_t pos) noexcept -> Result<void>
  {
    if (force_fail_) {
      return std::make_error_code(std::errc::io_error);
    }
    if (pos > data_.size()) {
      return std::make_error_code(std::errc::invalid_argument);
    }
    pos_ = pos;
    return {};
  }

  [[nodiscard]] auto Backward(size_t offset) noexcept -> Result<void>
  {
    if (force_fail_) {
      return std::make_error_code(std::errc::io_error);
    }
    if (offset > pos_) {
      return std::make_error_code(std::errc::io_error);
    }
    pos_ -= offset;
    return {};
  }

  [[nodiscard]] auto Forward(size_t offset) noexcept -> Result<void>
  {
    if (force_fail_) {
      return std::make_error_code(std::errc::io_error);
    }
    if (pos_ + offset > data_.size()) {
      return std::make_error_code(std::errc::io_error);
    }
    pos_ += offset;
    return {};
  }

  [[nodiscard]] auto SeekEnd() noexcept -> Result<void>
  {
    if (force_fail_) {
      return std::make_error_code(std::errc::io_error);
    }
    pos_ = data_.size();
    return {};
  }

  [[nodiscard]] auto Size() const noexcept -> Result<size_t>
  {
    if (force_fail_) {
      return std::make_error_code(std::errc::io_error);
    }
    return data_.size();
  }

  auto Reset() noexcept -> void
  {
    data_.clear();
    pos_ = 0;
    force_fail_ = false;
  }

  [[nodiscard]] auto EndOfStream() const noexcept -> bool
  {
    return pos_ >= data_.size();
  }

  // Testing helpers
  auto ForceFail(const bool fail) noexcept -> void { force_fail_ = fail; }

  [[nodiscard]] auto Data() const -> const std::vector<std::byte>&
  {
    return data_;
  }

private:
  std::vector<std::byte> data_;
  size_t pos_ = 0;
  bool force_fail_ = false;
};

static_assert(oxygen::serio::Stream<MockStream>);

} // namespace oxygen::content::testing
