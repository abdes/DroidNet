//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/base/win_errors.h"


auto oxygen::ComCategory() noexcept -> const std::error_category&
{
  static ComErrorCategory ecat;
  return ecat;
}

auto oxygen::ComToSystemError(const HRESULT hr, IErrorInfo* help) -> std::system_error
{
  using ComCstr = std::unique_ptr<OLECHAR[], decltype(SysFreeString)*>;
  auto get_description = [](IErrorInfo* info) -> ComCstr
    {
      BSTR description = nullptr;
      if (info) auto _ = info->GetDescription(&description);
      return { description, &SysFreeString };
    };

  ComCstr&& description = get_description(help);
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

    return ComToSystemError(hr, description.get());
  }

  return ComToSystemError(hr);
}
