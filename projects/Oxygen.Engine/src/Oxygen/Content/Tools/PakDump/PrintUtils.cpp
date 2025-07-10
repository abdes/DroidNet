//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iomanip>
#include <iostream>

#include "PrintUtils.h"

namespace PrintUtils {

void Separator(const std::string& title)
{
  fmt::print("{}\n", std::string(78, '='));
  if (!title.empty()) {
    fmt::print("== {}\n", title);
    fmt::print("{}\n", std::string(78, '='));
  }
}

void SubSeparator(const std::string& title)
{
  fmt::print("--- {} {}\n", title, std::string(70 - title.length(), '-'));
}

void Bytes(
  const std::string& name, const uint8_t* data, size_t size, int indent)
{
  std::string line = fmt::format("{:>{}}{}: ", "", indent, name);
  for (size_t i = 0; i < size; ++i) {
    if (i > 0 && i % 16 == 0) {
      fmt::print("{}\n", line);
      line = fmt::format("{:>{}}{}: ", "", indent, name);
    }
    line += fmt::format("{:02x} ", data[i]);
  }
  fmt::print("{}\n", line);
}

void HexDump(const uint8_t* data, size_t size, size_t max_bytes)
{
  size_t bytes_to_show = std::min(size, max_bytes);
  for (size_t i = 0; i < bytes_to_show; i += 16) {
    std::string line = fmt::format("{:>4}: {:08x} ", i, i);
    for (size_t j = 0; j < 16; ++j) {
      if (i + j < bytes_to_show) {
        line += fmt::format("{:02x} ", data[i + j]);
      } else {
        line += "   ";
      }
    }
    line += " ";
    for (size_t j = 0; j < 16 && i + j < bytes_to_show; ++j) {
      uint8_t c = data[i + j];
      line += (c >= 32 && c <= 126) ? static_cast<char>(c) : '.';
    }
    fmt::print("{}\n", line);
  }
  if (size > max_bytes) {
    std::cout << "    ... (" << (size - max_bytes) << " more bytes)\n";
  }
  std::cout << std::setfill(' ');
}

} // namespace PrintUtils
