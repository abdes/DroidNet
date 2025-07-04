//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Macros.h"

#include <span>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Base/api_export.h>

namespace oxygen::serio {

class MemoryStream {
public:
  OXYGEN_BASE_API explicit MemoryStream(
    std::span<std::byte> buffer = {}) noexcept;
  virtual ~MemoryStream() = default;

  OXYGEN_MAKE_NON_COPYABLE(MemoryStream);
  OXYGEN_DEFAULT_MOVABLE(MemoryStream);

  [[nodiscard]] auto write(std::span<const std::byte> data) noexcept
    -> Result<void>
  {
    return write(data.data(), data.size());
  }

  [[nodiscard]] OXYGEN_BASE_API auto write(
    const std::byte* data, size_t size) noexcept -> Result<void>;
  [[nodiscard]] OXYGEN_BASE_API auto read(std::byte* data, size_t size) noexcept
    -> Result<void>;
  [[nodiscard]] OXYGEN_BASE_API auto flush() const noexcept -> Result<void>;
  [[nodiscard]] OXYGEN_BASE_API auto position() const noexcept
    -> Result<size_t>;

  //! Sets the position of the next character to be extracted from the input
  //! stream.
  /*!
    \param pos The new absolute position within the stream.
    \return A Result<void> indicating success or failure.
    \retval std::errc::invalid_seek if the seek operation fails.
  */
  [[nodiscard]] OXYGEN_BASE_API auto seek(size_t pos) noexcept -> Result<void>;
  [[nodiscard]] OXYGEN_BASE_API auto size() const noexcept -> Result<size_t>;

  [[nodiscard]] OXYGEN_BASE_API auto data() const noexcept
    -> std::span<const std::byte>;
  OXYGEN_BASE_API void reset() noexcept;
  OXYGEN_BASE_API void clear();
  [[nodiscard]] OXYGEN_BASE_API auto eof() const noexcept -> bool;

  [[nodiscard]] OXYGEN_BASE_API auto backward(size_t offset) noexcept
    -> Result<void>;
  [[nodiscard]] OXYGEN_BASE_API auto forward(size_t offset) noexcept
    -> Result<void>;
  [[nodiscard]] OXYGEN_BASE_API auto seek_end() noexcept -> Result<void>;

private:
  std::vector<std::byte> internal_buffer_;
  std::span<std::byte> external_buffer_;
  size_t pos_ = 0;

  [[nodiscard]] auto get_buffer() noexcept -> std::span<std::byte>;
  [[nodiscard]] auto get_buffer() const noexcept -> std::span<const std::byte>;
};

static_assert(Stream<MemoryStream>);

} // namespace oxygen::serio
