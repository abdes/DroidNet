//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <filesystem>
#include <ios>
#include <optional>
#include <stdexcept>
#include <string_view>

#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h> // IWYU pragma: keep
#include <Oxygen/Data/TextureResourceDescriptor.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::internal {

auto LooseCookedSource::FindTextureIndexByVirtualPath(
  const std::string_view virtual_path) const
  -> std::optional<data::pak::core::ResourceIndexT>
{
  const auto relative = index_.FindTextureDescriptorRelPath(virtual_path);
  if (!relative) {
    return std::nullopt;
  }
  const auto path = cooked_root_ / std::filesystem::path(*relative);
  if (std::filesystem::file_size(base::ToNativePath(path))
    != data::kTextureResourceDescriptorSize) {
    throw std::runtime_error(
      "Texture descriptor does not use the current OTEX layout");
  }
  serio::FileStream<> stream(path, std::ios::in);
  serio::Reader reader(stream);
  const auto bytes = reader.ReadBlob(data::kTextureResourceDescriptorSize);
  if (!bytes) {
    throw std::runtime_error("Texture descriptor could not be read");
  }
  const auto decoded = data::DecodeTextureResourceDescriptor(*bytes);
  if (!decoded) {
    throw std::runtime_error(decoded.error());
  }
  const auto* table = GetTextureTable();
  if (decoded->index == data::pak::core::kNoResourceIndex || table == nullptr
    || !table->IsValidKey(decoded->index)) {
    throw std::runtime_error(
      "Texture descriptor index is outside its source texture table");
  }
  auto table_reader = CreateTextureTableReader();
  auto descriptor = data::pak::core::TextureResourceDesc {};
  const auto offset = table->GetResourceOffset(decoded->index);
  if (!table_reader || !offset || !table_reader->Seek(*offset)
    || !serio::Load(*table_reader, descriptor)
    || std::memcmp(&descriptor, &decoded->descriptor, sizeof(descriptor))
      != 0) {
    throw std::runtime_error(
      "Texture descriptor is stale or does not match its source table");
  }
  return decoded->index;
}

} // namespace oxygen::content::internal
