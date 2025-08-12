//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <span>

#include <Oxygen/Serio/MemoryStream.h>

namespace oxygen::serio {

MemoryStream::MemoryStream(const std::span<std::byte> buffer) noexcept
  : external_buffer_(buffer)
{
}

auto MemoryStream::GetBuffer() noexcept -> std::span<std::byte>
{
  return external_buffer_.empty() ? std::span(internal_buffer_)
                                  : external_buffer_;
}

auto MemoryStream::GetBuffer() const noexcept -> std::span<const std::byte>
{
  return external_buffer_.empty() ? std::span(internal_buffer_)
                                  : external_buffer_;
}

auto MemoryStream::Write(const std::byte* data, const size_t size) noexcept
  -> Result<void>
{
  if (data == nullptr && size > 0) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  if (size > (std::numeric_limits<size_t>::max)() - pos_) {
    return std::make_error_code(std::errc::value_too_large);
  }

  auto buffer = GetBuffer();
  if (pos_ + size > buffer.size()) {
    if (external_buffer_.empty()) {
      try {
        internal_buffer_.resize(pos_ + size);
      } catch (const std::bad_alloc&) {
        return std::make_error_code(std::errc::not_enough_memory);
      }
      buffer = std::span(internal_buffer_);
    } else {
      return std::make_error_code(std::errc::no_buffer_space);
    }
  }

  if (size > 0) {
    std::memcpy(buffer.data() + pos_, data, size);
  }
  pos_ += size;
  return {};
}

auto MemoryStream::Read(std::byte* data, const size_t size) noexcept
  -> Result<void>
{
  if (data == nullptr && size > 0) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  const auto buffer = GetBuffer();
  if (pos_ >= buffer.size()) {
    return std::make_error_code(std::errc::io_error);
  }

  const auto available = buffer.size() - pos_;
  const auto read_size = (std::min)(size, available);

  if (read_size < size) {
    return std::make_error_code(std::errc::io_error);
  }

  if (size > 0) {
    std::memcpy(data, buffer.data() + pos_, read_size);
  }
  pos_ += read_size;
  return {};
}

// ReSharper disable once CppMemberFunctionMayBeStatic
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto MemoryStream::Flush() noexcept -> Result<void> { return {}; }

auto MemoryStream::Position() const noexcept -> Result<size_t> { return pos_; }

auto MemoryStream::Seek(const size_t pos) noexcept -> Result<void>
{
  if (const auto buffer = GetBuffer(); pos > buffer.size()) {
    return std::make_error_code(std::errc::invalid_seek);
  }
  pos_ = pos;
  return {};
}

auto MemoryStream::Size() const noexcept -> Result<size_t>
{
  const auto buffer = GetBuffer();
  return buffer.size();
}

auto MemoryStream::Data() const noexcept -> std::span<const std::byte>
{
  const auto buffer = GetBuffer();
  return std::as_bytes(buffer);
}

auto MemoryStream::Reset() noexcept -> void { pos_ = 0; }

auto MemoryStream::Clear() -> void
{
  if (external_buffer_.empty()) {
    internal_buffer_.clear();
  } else {
    std::ranges::fill(external_buffer_, static_cast<std::byte>(0x00));
  }
  Reset();
}

auto MemoryStream::Backward(const size_t offset) noexcept -> Result<void>
{
  if (offset > pos_) {
    return std::make_error_code(std::errc::io_error);
  }
  pos_ -= offset;
  return {};
}

auto MemoryStream::Forward(const size_t offset) noexcept -> Result<void>
{
  const auto buffer = GetBuffer();
  if (pos_ + offset > buffer.size()) {
    return std::make_error_code(std::errc::io_error);
  }
  pos_ += offset;
  return {};
}

auto MemoryStream::SeekEnd() noexcept -> Result<void>
{
  const auto buffer = GetBuffer();
  pos_ = buffer.size();
  return {};
}

} // namespace oxygen::serio
