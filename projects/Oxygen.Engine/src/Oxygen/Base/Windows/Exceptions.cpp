//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/Platforms.h"
#if defined(OXYGEN_WINDOWS)

#  include "Oxygen/Base/Windows/Exceptions.h"

#  include <memory>
#  include <string>

#  include <fmt/format.h>

#  include "Oxygen/Base/Logging.h"
#  include "Oxygen/Base/StringUtils.h"

using oxygen::string_utils::WideToUtf8;
using oxygen::windows::WindowsException;

namespace {

struct LocalFreeHelper {
    void operator()(void* to_free) const
    {
        LocalFree(to_free);
    }
};

std::string GetErrorMessage(const DWORD error_code) noexcept
{
    std::unique_ptr<wchar_t[], LocalFreeHelper> msg_buffer {};
    LPWSTR buffer_allocated_mem { nullptr };
    const DWORD buffer_length = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        nullptr,
        error_code,
        0,
        reinterpret_cast<LPWSTR>(&buffer_allocated_mem), // null-terminated string
        0,
        nullptr);
    if (buffer_length == 0) {
        return fmt::format("__not_available__ (failed to get error message `{}`)", GetLastError());
    }
    DCHECK_EQ_F(buffer_length, ::wcslen(buffer_allocated_mem));

    msg_buffer.reset(buffer_allocated_mem);
    std::string message {};
    try {
        WideToUtf8(msg_buffer.get(), message);
        return message;
    } catch (const std::exception& e) {
        return fmt::format("__not_available__ ({})", e.what());
    }
}

} // namespace

std::exception_ptr WindowsException::FromErrorCode(const DWORD error_code) noexcept
{
    return std::make_exception_ptr(WindowsException(error_code));
}

const char* WindowsException::what() const noexcept
{
    if (!message_.has_value()) {
        message_ = fmt::format("{} : {}", code().value(), GetErrorMessage(code().value()));
    }
    return message_->c_str();
}

#endif // OXYGEN_WINDOWS
