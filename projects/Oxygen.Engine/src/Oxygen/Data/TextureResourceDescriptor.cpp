//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/TextureResourceDescriptor.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::data {

auto DecodeTextureResourceDescriptor(const std::span<const std::byte> bytes)
  -> Result<TextureResourceDescriptor, std::string>
{
  constexpr uint16_t kVersion = 1U;
  constexpr std::array kMagic { 'O', 'T', 'E', 'X' };
  if (bytes.size() != kTextureResourceDescriptorSize) {
    return Err(
      std::string("Texture descriptor size does not match the current format"));
  }
  auto buffer = std::vector<std::byte>(bytes.begin(), bytes.end());
  serio::MemoryStream stream { std::span<std::byte>(buffer) };
  serio::Reader reader(stream);
  const auto packed = reader.ScopedAlignment(1);
  auto magic = std::array<char, kMagic.size()> {};
  auto version = uint16_t {};
  auto reserved = uint16_t {};
  auto result = TextureResourceDescriptor {};
  if (!reader.ReadBlobInto(std::as_writable_bytes(std::span { magic }))
    || !reader.ReadInto(version) || !reader.ReadInto(reserved)
    || !reader.ReadInto(result.index)
    || !serio::Load(reader, result.descriptor)) {
    return Err(std::string("Texture descriptor could not be decoded"));
  }
  if (magic != kMagic || version != kVersion || reserved != 0U) {
    return Err(std::string(
      "Texture descriptor magic, version or reserved field is invalid"));
  }
  return Ok(result);
}

} // namespace oxygen::data
