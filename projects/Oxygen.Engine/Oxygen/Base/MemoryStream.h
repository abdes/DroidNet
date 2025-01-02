//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Macros.h"

#include <span>
#include <vector>

#include "Oxygen/api_export.h"
#include "Oxygen/Base/Result.h"
#include "Oxygen/Base/Stream.h"

namespace oxygen::serio {

class MemoryStream
{
 public:
  OXYGEN_API explicit MemoryStream(std::span<char> buffer = {}) noexcept;
  OXYGEN_API virtual ~MemoryStream() = default;

  OXYGEN_MAKE_NON_COPYABLE(MemoryStream);
  OXYGEN_DEFAULT_MOVABLE(MemoryStream);

  OXYGEN_API [[nodiscard]] auto write(const char* data, size_t size) noexcept -> Result<void>;
  OXYGEN_API [[nodiscard]] auto read(char* data, size_t size) noexcept -> Result<void>;
  OXYGEN_API [[nodiscard]] auto flush() const noexcept -> Result<void>;
  OXYGEN_API [[nodiscard]] auto position() const noexcept -> Result<size_t>;

  //! Sets the position of the next character to be extracted from the input stream.
  /*!
    \param pos The new absolute position within the stream.
    \return A Result<void> indicating success or failure.
    \retval std::errc::invalid_seek if the seek operation fails.
  */
  OXYGEN_API [[nodiscard]] auto seek(size_t pos) noexcept -> Result<void>;
  OXYGEN_API [[nodiscard]] auto size() const noexcept -> Result<size_t>;

  OXYGEN_API [[nodiscard]] auto data() const noexcept -> std::span<const std::byte>;
  OXYGEN_API void reset() noexcept;
  OXYGEN_API void clear();
  OXYGEN_API [[nodiscard]] auto eof() const noexcept -> bool;

 private:
  std::vector<char> internal_buffer_;
  std::span<char> external_buffer_;
  size_t pos_ = 0;

  [[nodiscard]] auto get_buffer() noexcept -> std::span<char>;
  [[nodiscard]] auto get_buffer() const noexcept -> std::span<const char>;
};

static_assert(Stream<MemoryStream>);

} // namespace oxygen::serio