//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <fmt/format.h>

namespace PrintUtils {

void Separator(const std::string& title = "");
void SubSeparator(const std::string& title);
template <typename T>
void Field(const std::string& name, const T& value, int indent = 4)
{
  fmt::print("{:>{}}{:<20}{}\n", "", indent, name + ":", value);
}

void Bytes(
  const std::string& name, const uint8_t* data, size_t size, int indent = 4);
void HexDump(const uint8_t* data, size_t size, size_t max_bytes = 256);

} // namespace PrintUtils
