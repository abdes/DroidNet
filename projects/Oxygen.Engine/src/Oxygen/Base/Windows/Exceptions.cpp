//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Platforms.h>
#if defined(OXYGEN_WINDOWS)

#  include <Oxygen/Base/Windows/Exceptions.h>

#  include <memory>
#  include <string>

#  include <fmt/format.h>

#  include <Oxygen/Base/Finally.h>
#  include <Oxygen/Base/Logging.h>
#  include <Oxygen/Base/StringUtils.h>

using oxygen::string_utils::WideToUtf8;
using oxygen::windows::WindowsException;

namespace {

struct LocalFreeHelper {
    void operator()(void* to_free) const
    {
        LocalFree(to_free);
    }
};

auto GetErrorMessage(const DWORD error_code) -> std::string
{
    try {
        LPWSTR buffer { nullptr };

        const DWORD buffer_length = FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
            nullptr,
            error_code,
            0,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<LPWSTR>(&buffer),
            0,
            nullptr);

        if (buffer_length == 0) {
            return fmt::format("__not_available__ (failed to get error message `{}`)",
                GetLastError());
        }

        // Ensure buffer is freed when we exit this scope
        auto cleanup = oxygen::Finally([buffer]() {
            if (buffer) {
                LocalFree(buffer);
            }
        });

        std::string message {};
        WideToUtf8(buffer, message);
        return message;

    } catch (const std::exception& e) {
        return fmt::format("__not_available__ ({})", e.what());
    }
}

} // namespace

auto WindowsException::FromErrorCode(const DWORD error_code) noexcept -> std::exception_ptr
{
    return std::make_exception_ptr(WindowsException(error_code));
}

auto WindowsException::what() const noexcept -> const char*
{
    if (!message_.has_value()) {
        message_ = fmt::format("{} : {}", code().value(), GetErrorMessage(code().value()));
    }
    return message_->c_str();
}

#endif // OXYGEN_WINDOWS
