//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <span>

#include "Oxygen/Base/MemoryStream.h"

namespace oxygen::serio {

MemoryStream::MemoryStream(const std::span<char> buffer) noexcept
    : external_buffer_(buffer)
{
}

auto MemoryStream::get_buffer() noexcept -> std::span<char>
{
    return external_buffer_.empty() ? std::span(internal_buffer_) : external_buffer_;
}

auto MemoryStream::get_buffer() const noexcept -> std::span<const char>
{
    return external_buffer_.empty() ? std::span(internal_buffer_) : external_buffer_;
}

auto MemoryStream::write(const char* data, const size_t size) noexcept -> Result<void>
{
    if (data == nullptr && size > 0) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (size > std::numeric_limits<size_t>::max() - pos_) {
        return std::make_error_code(std::errc::value_too_large);
    }

    auto buffer = get_buffer();
    if (pos_ + size > buffer.size()) {
        if (external_buffer_.empty()) {
            internal_buffer_.resize(pos_ + size);
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

auto MemoryStream::read(char* data, const size_t size) noexcept -> Result<void>
{
    if (data == nullptr && size > 0) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    const auto buffer = get_buffer();
    if (pos_ >= buffer.size()) {
        return std::make_error_code(std::errc::io_error);
    }

    const auto available = buffer.size() - pos_;
    const auto read_size = std::min(size, available);

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
auto MemoryStream::flush() const noexcept -> Result<void>
{
    return {};
}

auto MemoryStream::position() const noexcept -> Result<size_t>
{
    return pos_;
}

auto MemoryStream::seek(const size_t pos) noexcept -> Result<void>
{
    if (const auto buffer = get_buffer(); pos > buffer.size()) {
        return std::make_error_code(std::errc::invalid_seek);
    }
    pos_ = pos;
    return {};
}

auto MemoryStream::size() const noexcept -> Result<size_t>
{
    const auto buffer = get_buffer();
    return buffer.size();
}

auto MemoryStream::data() const noexcept -> std::span<const std::byte>
{
    const auto buffer = get_buffer();
    return std::as_bytes(buffer);
}

void MemoryStream::reset() noexcept
{
    pos_ = 0;
}

void MemoryStream::clear()
{
    if (external_buffer_.empty()) {
        internal_buffer_.clear();
    } else {
        std::ranges::fill(external_buffer_, static_cast<char>(0));
    }
    reset();
}

auto MemoryStream::eof() const noexcept -> bool
{
    const auto buffer = get_buffer();
    return pos_ >= buffer.size();
}

} // namespace oxygen::serio
