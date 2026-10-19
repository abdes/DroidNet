//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

#include <cstddef>
#include <string>
#include <string_view>

namespace oxygen::interop {

//! Converts managed text to the UTF-8 the engine stores; null is empty.
inline auto ToUtf8(System::String ^ text) -> std::string
{
  if (System::String::IsNullOrEmpty(text)) {
    return {};
  }
  const auto bytes = System::Text::Encoding::UTF8->GetBytes(text);
  const pin_ptr<System::Byte> pinned = &bytes[0];
  return std::string(reinterpret_cast<const char*>(pinned),
    static_cast<std::size_t>(bytes->Length));
}

//! Converts engine UTF-8 text to managed text.
inline auto FromUtf8(std::string_view text) -> System::String ^
{
  if (text.empty()) {
    return System::String::Empty;
  }
  return gcnew System::String(text.data(), 0, static_cast<int>(text.size()),
    System::Text::Encoding::UTF8);
}

} // namespace oxygen::interop

#pragma managed(pop)
