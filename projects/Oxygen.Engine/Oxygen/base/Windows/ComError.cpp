//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/Platforms.h"
#if defined(OXYGEN_WINDOWS)

#include "Oxygen/Base/Windows/ComError.h"
#include "Oxygen/Base/Windows/StringUtils.h"
#include "Oxygen/Base/Logging.h"

using oxygen::string_utils::WideToUtf8;

namespace {

  auto GetComErrorMessage(const HRESULT hr, IErrorInfo* help) -> std::string
  {
    using ComStringPtr = std::unique_ptr<OLECHAR[], decltype(SysFreeString)*>;
    auto get_description = [](IErrorInfo* info) -> ComStringPtr
      {
        BSTR description = nullptr;
        if (info) auto _ = info->GetDescription(&description);
        return { description, &SysFreeString };
      };

    ComStringPtr&& description = get_description(help);
    if (const unsigned int length = description ? SysStringLen(description.get()) : 0) {
      const unsigned int n = length;
      const OLECHAR ch0 = std::exchange(description[0], L'\0');
      for (;;) {
        if (description[n - 1] == L'\r' || description[n - 1] == L'\n' || description[n - 1] == L'.') {
          continue;
        }
        break;
      }
      if (n < length && n) description[n] = L'\0';
      if (n) description[0] = ch0;

      std::string utf_8description;
      try {
        WideToUtf8(description.get(), utf_8description);
      }
      catch (const std::exception& e) {
        LOG_F(WARNING, "Failed to convert wide string to UTF-8: {}", e.what());
        utf_8description.append("__not_available__ (").append(e.what()).append(")");
      }
      return utf_8description;
    }

    std::string utf8_error_message;
    try {
      WideToUtf8(_com_error(hr).ErrorMessage(), utf8_error_message);
    }
    catch (const std::exception& e) {
      LOG_F(WARNING, "Failed to convert wide string to UTF-8: {}", e.what());
      utf8_error_message.append("__not_available__ (").append(e.what()).append(")");
    }
    return utf8_error_message;
  }

} // namespace

auto oxygen::windows::ComCategory() noexcept -> const std::error_category&
{
  static windows::ComErrorCategory com_error_category;
  return com_error_category;
}

auto oxygen::windows::ComErrorCategory::message(const int hr) const -> std::string
{
  std::string utf8_message;
  try {
    WideToUtf8(_com_error{ hr }.ErrorMessage(), utf8_message);
  }
  catch (const std::exception& e) {
    LOG_F(WARNING, "Failed to convert wide string to UTF-8: {}", e.what());
    utf8_message.append("__not_available__ (").append(e.what()).append(")");
  }
  return utf8_message;
}

auto oxygen::windows::ComError::what() const noexcept -> const char*
{
  return (!message_) ? std::system_error::what() : message_->c_str();
}

void oxygen::windows::detail::HandleComErrorImpl(HRESULT hr, const std::string& utf8_message) {
  if (!utf8_message.empty()) {
    LOG_F(ERROR, "{}", utf8_message);
  }

  if (FAILED(hr)) {
    std::string error_message{};

    // Query for IErrorInfo
    IErrorInfo* p_error_info = nullptr;
    if (const auto err_info_hr = GetErrorInfo(0, &p_error_info); err_info_hr == S_OK) {
      // Retrieve error information
      error_message.assign(GetComErrorMessage(hr, p_error_info));
      LOG_F(ERROR, "COM Error: 0x{:x} - {}", hr, error_message);
      p_error_info->Release();
    }
    else {
      error_message.assign(fmt::format("COM Error: 0x{:x} - (no description)", hr));
      LOG_F(ERROR, "COM Error: 0x{:x} - (no description)", hr);
    }
    ComError::Throw(static_cast<ComErrorEnum>(hr), error_message);
  }
}

#endif // OXYGEN_WINDOWS
