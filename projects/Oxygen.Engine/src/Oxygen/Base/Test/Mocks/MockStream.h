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
#include <Oxygen/Base/Stream.h>

namespace oxygen::serio::testing {

class MockStream {
public:
    explicit MockStream() = default;

    auto write(const char* data, const size_t size) noexcept -> Result<void>
    {
        try {
            if (force_fail_) {
                return std::make_error_code(std::errc::io_error);
            }

            // Check for size limits
            using difference_type = std::vector<char>::difference_type;
            if (pos_ > static_cast<size_t>(std::numeric_limits<difference_type>::max())
                || size > static_cast<size_t>(std::numeric_limits<difference_type>::max())) {
                return std::make_error_code(std::errc::value_too_large);
            }

            // Ensure buffer has enough space
            if (pos_ + size > data_.size()) {
                data_.resize(pos_ + size);
            }

            // Write at current position
            std::span<const char> input_data { data, size };
            auto dest = std::ranges::subrange(
                data_.begin() + static_cast<difference_type>(pos_),
                data_.begin() + static_cast<difference_type>(pos_ + size));
            std::ranges::copy(input_data, dest.begin());
            pos_ += size;
            return {};
        } catch (const std::exception& /*ex*/) {
            return std::make_error_code(std::errc::io_error);
        }
    }

    auto read(char* data, const size_t size) noexcept -> Result<void>
    {
        if (force_fail_) {
            return std::make_error_code(std::errc::io_error);
        }

        // Check for size limits
        using difference_type = std::vector<char>::difference_type;
        if (pos_ > static_cast<size_t>(std::numeric_limits<difference_type>::max())
            || size > static_cast<size_t>(std::numeric_limits<difference_type>::max())) {
            return std::make_error_code(std::errc::value_too_large);
        }

        // Check if we have enough data to read
        if (pos_ + size > data_.size()) {
            return std::make_error_code(std::errc::no_buffer_space);
        }

        // Read at current position
        auto source = std::ranges::subrange(
            data_.begin() + static_cast<difference_type>(pos_),
            data_.begin() + static_cast<difference_type>(pos_ + size));
        std::span<char> output_data { data, size };

        std::ranges::copy(source, output_data.begin());
        pos_ += size;
        return {};
    }

    // NOLINTNEXTLINE(readability-make-member-function-const)
    auto flush() noexcept -> Result<void>
    {
        if (force_fail_) {
            return std::make_error_code(std::errc::io_error);
        }
        return {};
    }

    [[nodiscard]] auto position() const noexcept -> Result<size_t>
    {
        if (force_fail_) {
            return std::make_error_code(std::errc::io_error);
        }
        return pos_;
    }

    auto seek(const size_t pos) noexcept -> Result<void>
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

    [[nodiscard]] auto size() const noexcept -> Result<size_t>
    {
        if (force_fail_) {
            return std::make_error_code(std::errc::io_error);
        }
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
