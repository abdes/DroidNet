//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::testing {

// Parses a hexdump with decimal offsets (e.g. "   0: 4F 58 ...") into a byte
// buffer.
inline auto ParseHexDumpWithOffset(const std::string& hexdump)
  -> std::vector<std::byte>
{
  std::vector<std::byte> buffer;
  std::istringstream iss(hexdump);
  std::string line;
  while (std::getline(iss, line)) {
    // Find the colon (':') that separates the offset from the bytes
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue; // skip lines without offset
    }

    // Start parsing after the colon
    std::istringstream bytes(line.substr(colon + 1));
    std::string byte_str;
    while (bytes >> byte_str) {
      if (byte_str.size() != 2) {
        continue; // skip non-byte tokens
      }
      // Convert hex string to byte
      const auto* const last = std::to_address(byte_str.cend());
      auto value = uint8_t {};
      const auto parsed = std::from_chars(byte_str.data(), last, value, 16);
      if (parsed.ec != std::errc {} || parsed.ptr != last) {
        continue; // skip invalid hex
      }
      buffer.push_back(std::byte { value });
    }
  }
  return buffer;
}

// Overload: Parses a hexdump and pads the buffer to total_size with pad_byte if
// needed.
inline auto ParseHexDumpWithOffset(const std::string& hexdump,
  const int total_size, const std::byte pad_byte = std::byte { 0 })
  -> std::vector<std::byte>
{
  auto buffer = ParseHexDumpWithOffset(hexdump);
  if (total_size > 0 && std::cmp_less(buffer.size(), total_size)) {
    buffer.resize(total_size, pad_byte);
  }
  return buffer;
}

template <serio::OutputStream DescS, serio::OutputStream DataS>
auto WriteDescriptorWithData(serio::Writer<DescS>& desc_writer,
  serio::Writer<DataS>& data_writer, const std::string& hexdump,
  const uint32_t size_bytes, const std::byte data_filler)
{
  // Parse the header
  std::vector<std::byte> buffer = ParseHexDumpWithOffset(hexdump);
  {
    auto pack = desc_writer.ScopedAlignment(1);
    auto result = desc_writer.WriteBlob(buffer);
    if (!result) {
      throw std::runtime_error("Failed to write texture descriptor to stream: "
        + result.error().message());
    }
  }
  {
    buffer.resize(size_bytes);
    std::ranges::fill(buffer, data_filler);
    auto pack = data_writer.ScopedAlignment(256);
    auto result = data_writer.WriteBlob(buffer);
    if (!result) {
      throw std::runtime_error(
        "Failed to write texture data to stream: " + result.error().message());
    }
  }
}

} // namespace oxygen::content::testing
