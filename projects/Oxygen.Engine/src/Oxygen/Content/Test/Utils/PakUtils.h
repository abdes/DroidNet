//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Data/PakFormat.h>
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
      char* end = nullptr;
      int value = std::strtol(byte_str.c_str(), &end, 16);
      if (end != byte_str.c_str() + 2) {
        continue; // skip invalid hex
      }
      buffer.push_back(static_cast<std::byte>(value));
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

inline auto MakeV4TexturePayload(const uint32_t data_size,
  const std::byte fill_value,
  const oxygen::data::pak::TexturePackingPolicyId policy
  = oxygen::data::pak::TexturePackingPolicyId::kD3D12,
  const uint32_t row_pitch_bytes = 0U) -> std::vector<uint8_t>
{
  using oxygen::data::pak::SubresourceLayout;
  using oxygen::data::pak::TexturePackingPolicyId;
  using oxygen::data::pak::TexturePayloadFlags;
  using oxygen::data::pak::TexturePayloadHeader;

  TexturePayloadHeader header {};
  header.magic = oxygen::data::pak::kTexturePayloadMagic;
  header.packing_policy = static_cast<uint8_t>(policy);
  header.flags = static_cast<uint8_t>(TexturePayloadFlags::kNone);
  header.subresource_count = 1;
  header.layouts_offset_bytes = static_cast<uint32_t>(sizeof(header));
  header.data_offset_bytes = header.layouts_offset_bytes
    + static_cast<uint32_t>(sizeof(SubresourceLayout));
  header.total_payload_size = header.data_offset_bytes + data_size;

  SubresourceLayout layout {};
  layout.offset_bytes = 0;
  layout.row_pitch_bytes = row_pitch_bytes != 0U ? row_pitch_bytes : data_size;
  layout.size_bytes = data_size;

  std::vector<uint8_t> payload(header.total_payload_size, 0);
  std::memcpy(payload.data(), &header, sizeof(header));
  std::memcpy(
    payload.data() + header.layouts_offset_bytes, &layout, sizeof(layout));
  std::ranges::fill(payload.begin() + header.data_offset_bytes, payload.end(),
    static_cast<uint8_t>(fill_value));
  return payload;
}

template <serio::Stream DescS, serio::Stream DataS>
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
