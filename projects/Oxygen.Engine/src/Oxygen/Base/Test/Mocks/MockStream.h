//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Result.h"
#include "Oxygen/Base/Stream.h"
#include <vector>

namespace oxygen::serio::testing {

class MockStream {
public:
    explicit MockStream() = default;

    auto write(const char* data, const size_t size) noexcept -> Result<void>
    {
        if (force_fail_)
            return std::make_error_code(std::errc::io_error);

        // Ensure buffer has enough space
        if (pos_ + size > data_.size()) {
            data_.resize(pos_ + size);
        }

        // Write at current position
        std::memcpy(data_.data() + pos_, data, size);
        pos_ += size;
        return {};
    }

    auto read(char* data, const size_t size) noexcept -> Result<void>
    {
        if (force_fail_)
            return std::make_error_code(std::errc::io_error);
        if (pos_ + size > data_.size())
            return std::make_error_code(std::errc::no_buffer_space);

        std::memcpy(data, data_.data() + pos_, size);
        pos_ += size;
        return {};
    }

    auto flush() noexcept -> Result<void>
    {
        if (force_fail_)
            return std::make_error_code(std::errc::io_error);
        return {};
    }

    [[nodiscard]] auto position() const noexcept -> Result<size_t>
    {
        if (force_fail_)
            return std::make_error_code(std::errc::io_error);
        return pos_;
    }

    auto seek(const size_t pos) noexcept -> Result<void>
    {
        if (force_fail_)
            return std::make_error_code(std::errc::io_error);
        if (pos > data_.size())
            return std::make_error_code(std::errc::invalid_argument);
        pos_ = pos;
        return {};
    }

    [[nodiscard]] auto size() const noexcept -> Result<size_t>
    {
        if (force_fail_)
            return std::make_error_code(std::errc::io_error);
        return data_.size();
    }

    [[nodiscard]] auto eof() const noexcept -> bool
    {
        return pos_ >= data_.size();
    }

    // Testing helpers
    void force_fail(const bool fail) noexcept { force_fail_ = fail; }
    [[nodiscard]] auto get_data() const -> const std::vector<char>& { return data_; }

private:
    std::vector<char> data_;
    size_t pos_ = 0;
    bool force_fail_ = false;
};

static_assert(Stream<MockStream>);

} // namespace oxygen::serio::testing
