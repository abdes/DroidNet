//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Content/Internal/ResourceTypes.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/MappedFile.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/RegionReader.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::internal {

namespace {

  auto MapTable(const std::filesystem::path& path) -> serio::MappedFile
  {
    auto file = serio::MappedFile::Open(path);
    if (!file) {
      throw std::system_error(
        file.error(), "Could not map resource table '" + path.string() + "'");
    }
    return std::move(file).value();
  }

  // A mounted root stays importable: an import appends to a data file while
  // the mount reads it, and appending never changes what the index points at.
  constexpr auto kDataSpec = serio::OpenSpec {
    .sharing = serio::Sharing::kReadWriteDelete,
  };

  auto OpenData(const std::filesystem::path& path, serio::IoDriver& driver)
    -> serio::AnyAsyncDevice
  {
    auto file = driver.Open(path, kDataSpec);
    if (!file) {
      throw std::system_error(
        file.error(), "Could not open resource data '" + path.string() + "'");
    }
    return serio::AnyAsyncDevice(std::move(file).value());
  }

} // namespace

auto LooseCookedSource::OpenFiles() -> void
{
  if (!driver_) {
    throw std::invalid_argument(
      "A loose cooked source that reads payloads needs an IO driver");
  }
  const auto open
    = [this](ResourceFiles& files,
        const std::optional<std::filesystem::path>& table,
        const std::optional<std::filesystem::path>& data) -> void {
    if (table) {
      files.table = MapTable(*table);
    }
    if (data) {
      files.data = OpenData(*data, *driver_);
      auto whole = serio::Region<serio::AnyAsyncDevice>::Whole(*files.data);
      if (!whole) {
        throw std::system_error(whole.error(),
          "Could not size resource data '" + data->string() + "'");
      }
      files.payloads = *whole;
    }
  };
  open(
    FilesOf<data::BufferResource>(), buffers_table_path_, buffers_data_path_);
  open(FilesOf<data::TextureResource>(), textures_table_path_,
    textures_data_path_);
  open(
    FilesOf<data::ScriptResource>(), scripts_table_path_, scripts_data_path_);
  open(
    FilesOf<data::PhysicsResource>(), physics_table_path_, physics_data_path_);
}

auto LooseCookedSource::FilesOf(const TypeId resource_type) const noexcept
  -> const ResourceFiles*
{
  return VisitResourceType(
    resource_type,
    [this]<typename T> -> const ResourceFiles* {
      return &files_.at(kResourceTypeIndex<T>);
    },
    [] -> const ResourceFiles* { return nullptr; });
}

auto LooseCookedSource::ResourceRowSpace(
  const TypeId resource_type) const noexcept -> std::span<const std::byte>
{
  // Loose table offsets are offsets of the table file.
  const auto* files = FilesOf(resource_type);
  return files != nullptr && files->table ? files->table->Bytes()
                                          : std::span<const std::byte> {};
}

auto LooseCookedSource::PayloadRegion(const TypeId resource_type) const noexcept
  -> std::optional<serio::Region<serio::AnyAsyncDevice>>
{
  // A loose descriptor's data offset is an offset of its data file, sized
  // once, when the file was opened at mount.
  const auto* files = FilesOf(resource_type);
  return files != nullptr ? files->payloads : std::nullopt;
}

auto LooseCookedSource::OpenDescriptor(const std::filesystem::path& path)
  -> std::unique_ptr<serio::AnyReader>
{
  auto file = serio::NativeFile::Open(path, {});
  if (!file) {
    throw std::system_error(
      file.error(), "Could not open asset descriptor '" + path.string() + "'");
  }
  auto reader
    = serio::RegionReader<serio::NativeFile>::Owning(std::move(file).value());
  if (!reader) {
    throw std::system_error(reader.error(),
      "Could not read asset descriptor '" + path.string() + "'");
  }
  return std::move(reader).value();
}

auto LooseCookedSource::FindTextureIndexByVirtualPath(
  const std::string_view virtual_path) const
  -> std::optional<data::pak::core::ResourceIndexT>
{
  const auto relative = index_.FindTextureDescriptorRelPath(virtual_path);
  if (!relative) {
    return std::nullopt;
  }
  const auto path = cooked_root_ / std::filesystem::path(*relative);
  const auto file = serio::NativeFile::Open(path, {});
  if (!file) {
    throw std::system_error(file.error(),
      "Could not open texture descriptor '" + path.string() + "'");
  }
  const auto size = file->Size();
  if (!size || size->get() != data::kTextureResourceDescriptorSize) {
    throw std::runtime_error(
      "Texture descriptor does not use the current OTEX layout");
  }
  auto bytes = std::array<std::byte, data::kTextureResourceDescriptorSize> {};
  if (!serio::ReadExactlyAt(*file, serio::ByteOffset { 0 }, bytes)) {
    throw std::runtime_error("Texture descriptor could not be read");
  }
  const auto decoded = data::DecodeTextureResourceDescriptor(bytes);
  if (!decoded) {
    throw std::runtime_error(decoded.error());
  }
  const auto* table = GetTextureTable();
  if (decoded->index == data::pak::core::kNoResourceIndex || table == nullptr
    || !table->IsValidKey(decoded->index)) {
    throw std::runtime_error(
      "Texture descriptor index is outside its source texture table");
  }
  // An index-only source maps no table at mount; it maps this one to compare.
  const auto& textures = files_.at(kResourceTypeIndex<data::TextureResource>);
  if (!textures.table && !textures_table_path_) {
    throw std::runtime_error("Texture descriptor has no source textures table");
  }
  const auto mapped = textures.table
    ? std::optional<serio::MappedFile> {}
    : std::optional { MapTable(*textures_table_path_) };
  const auto table_bytes = mapped ? mapped->Bytes() : textures.table->Bytes();
  const auto offset = table->GetResourceOffset(decoded->index);
  constexpr auto kRowSize = sizeof(decoded->descriptor);
  if (!offset || *offset > table_bytes.size()
    || kRowSize > table_bytes.size() - *offset
    || std::memcmp(table_bytes.subspan(static_cast<size_t>(*offset)).data(),
         &decoded->descriptor, kRowSize)
      != 0) {
    throw std::runtime_error(
      "Texture descriptor is stale or does not match its source table");
  }
  return decoded->index;
}

} // namespace oxygen::content::internal
