//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "oxygen/base/logging.h"
#include "oxygen/base/win_errors.h"

namespace oxygen::renderer::direct3d12 {

  inline auto ToNarrow(const std::wstring& wide_string) -> std::string
  {
    if (wide_string.empty()) return {};

    const int size_needed = WideCharToMultiByte(
      CP_UTF8,
      0,
      wide_string.data(),
      static_cast<int>(wide_string.size()),
      nullptr,
      0,
      nullptr,
      nullptr);
    std::string utf8_string(size_needed, 0);

    WideCharToMultiByte(
      CP_UTF8,
      0,
      wide_string.data(),
      static_cast<int>(wide_string.size()),
      utf8_string.data(),
      size_needed,
      nullptr,
      nullptr);

    return utf8_string;
  }

  inline void NameObject(ID3D12Object* const object, const std::wstring& name)
  {
#ifdef _DEBUG
    CheckResult(object->SetName(name.c_str()));
    LOG_F(1, "+D3D12 named object created: {}", ToNarrow(name));
#endif
  }

}  // namespace oxygen::renderer::direct3d12
