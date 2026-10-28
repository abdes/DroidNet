//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>

#include <fmt/format.h>

namespace PrintUtils {

void Separator(std::ostream& out, const std::string& title = "");
void SubSeparator(std::ostream& out, const std::string& title);
template <typename T>
void Field(
  std::ostream& out, const std::string& name, const T& value, int indent = 4)
{
  out << fmt::format("{:>{}}{:<20}{}\n", "", indent, name + ":", value);
}

void Bytes(std::ostream& out, const std::string& name,
  std::span<const uint8_t> data, int indent = 4);
void HexDump(
  std::ostream& out, const uint8_t* data, size_t size, size_t max_bytes = 256);
//! Hex dump of `data`, at most `max_bytes` of it.
void HexDump(
  std::ostream& out, std::span<const std::byte> data, size_t max_bytes = 256);

} // namespace PrintUtils
