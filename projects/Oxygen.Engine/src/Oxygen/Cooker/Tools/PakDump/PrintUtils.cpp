//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <span>
#include <string>

#include "PrintUtils.h"
#include <fmt/base.h>
#include <fmt/format.h>

namespace PrintUtils {

void Separator(std::ostream& out, const std::string& title)
{
  out << fmt::format("{}\n", std::string(78, '='));
  if (!title.empty()) {
    out << fmt::format("== {}\n", title);
    out << fmt::format("{}\n", std::string(78, '='));
  }
}

void SubSeparator(std::ostream& out, const std::string& title)
{
  out << fmt::format(
    "--- {} {}\n", title, std::string(70 - title.length(), '-'));
}

void Bytes(std::ostream& out, const std::string& name,
  const std::span<const uint8_t> data, const int indent)
{
  std::string line = fmt::format("{:>{}}{}: ", "", indent, name);
  for (size_t i = 0; i < data.size(); ++i) {
    if (i > 0 && i % 16 == 0) {
      out << fmt::format("{}\n", line);
      line = fmt::format("{:>{}}{}: ", "", indent, name);
    }
    line += fmt::format("{:02x} ", data[i]);
  }
  out << fmt::format("{}\n", line);
}

void HexDump(
  std::ostream& out, const uint8_t* data, size_t size, size_t max_bytes)
{
  HexDump(out, std::as_bytes(std::span(data, size)), max_bytes);
}

void HexDump(std::ostream& out, const std::span<const std::byte> data,
  const size_t max_bytes)
{
  const auto shown = data.first((std::min)(data.size(), max_bytes));
  for (size_t i = 0; i < shown.size(); i += 16) {
    std::string line = fmt::format("{:>4}: {:08x} ", i, i);
    for (size_t j = 0; j < 16; ++j) {
      if (i + j < shown.size()) {
        line += fmt::format("{:02x} ", std::to_integer<uint8_t>(shown[i + j]));
      } else {
        line += "   ";
      }
    }
    line += ' ';
    for (size_t j = 0; j < 16 && i + j < shown.size(); ++j) {
      const auto c = std::to_integer<uint8_t>(shown[i + j]);
      line += (c >= 32 && c <= 126) ? static_cast<char>(c) : '.';
    }
    out << fmt::format("{}\n", line);
  }
  if (data.size() > max_bytes) {
    out << "    ... (" << (data.size() - max_bytes) << " more bytes)\n";
  }
  out << std::setfill(' ');
}

} // namespace PrintUtils
