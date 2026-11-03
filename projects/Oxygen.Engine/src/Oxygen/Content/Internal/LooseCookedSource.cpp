//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Content/Internal/ResourceTypes.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
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
  // the mount reads it, and appending never changes what the index points at;
  // the next writer truncates only bytes no commit records.
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

  auto CorruptTextureDescriptor(const data::CookedDefect defect,
    const std::filesystem::path& path, const std::string_view what)
    -> data::CookedFormatError
  {
    return { defect,
      "Texture descriptor '" + path.string() + "' " + std::string(what) };
  }

  auto ReadTextureDescriptor(const std::filesystem::path& path)
    -> data::TextureResourceDescriptor
  {
    const auto file = serio::NativeFile::Open(path, {});
    if (!file) {
      throw std::system_error(file.error(),
        "Could not open texture descriptor '" + path.string() + "'");
    }
    const auto size = file->Size();
    if (!size) {
      throw std::system_error(size.error(),
        "Could not size texture descriptor '" + path.string() + "'");
    }
    // One byte past a descriptor lets the decoder tell another format or
    // version from a malformed size.
    constexpr auto kReadLimit
      = uint64_t { data::kTextureResourceDescriptorSize } + 1U;
    auto bytes = std::vector<std::byte>(
      static_cast<size_t>(std::min(size->get(), kReadLimit)));
    if (const auto read
      = serio::ReadExactlyAt(*file, serio::ByteOffset { 0 }, bytes);
      !read) {
      throw std::system_error(read.error(),
        "Could not read texture descriptor '" + path.string() + "'");
    }
    const auto decoded = data::DecodeTextureResourceDescriptor(bytes);
    if (!decoded) {
      throw data::CookedFormatError(decoded.error());
    }
    return *decoded;
  }

  auto TextureRowOffsetOrThrow(const std::filesystem::path& path,
    const ResourceTable<data::TextureResource>* table,
    const data::pak::core::ResourceIndexT index) -> data::pak::core::OffsetT
  {
    if (table == nullptr) {
      throw CorruptTextureDescriptor(
        data::CookedDefect::kMissingPart, path, "has no source textures table");
    }
    const auto offset = index == data::pak::core::kNoResourceIndex
      ? std::nullopt
      : table->GetResourceOffset(index);
    if (!offset) {
      throw CorruptTextureDescriptor(data::CookedDefect::kOutOfBounds, path,
        "names a row outside its source textures table");
    }
    return *offset;
  }

  auto ValidateTextureTableRow(const std::filesystem::path& path,
    const std::span<const std::byte> table_bytes,
    const data::pak::core::OffsetT row_offset,
    const data::pak::core::TextureResourceDesc& descriptor) -> void
  {
    const auto expected = std::as_bytes(std::span { &descriptor, 1 });
    const auto row_in_file = row_offset <= table_bytes.size()
      && expected.size() <= table_bytes.size() - row_offset;
    if (!row_in_file) {
      throw CorruptTextureDescriptor(data::CookedDefect::kOutOfBounds, path,
        "names a row its textures table file does not hold");
    }
    const auto row
      = table_bytes.subspan(static_cast<size_t>(row_offset), expected.size());
    if (!std::ranges::equal(row, expected)) {
      throw CorruptTextureDescriptor(data::CookedDefect::kInconsistent, path,
        "is stale: its row differs from its source textures table");
    }
  }

} // namespace

auto LooseCookedSource::OpenFiles() -> void
{
  if (!driver_) {
    throw std::invalid_argument(
      "A loose cooked source that reads payloads needs an IO driver");
  }
  using data::loose_cooked::FileKind;
  const auto open = [this](ResourceFiles& files,
                      const std::optional<std::filesystem::path>& table,
                      const std::optional<std::filesystem::path>& data,
                      const FileKind data_kind) -> void {
    if (table) {
      files.table = MapTable(*table);
    }
    if (data) {
      files.data = OpenData(*data, *driver_);
      // The committed length: bytes a writer appends past it are no
      // commit's.
      const auto committed = index_.FindFileSize(data_kind).value_or(0U);
      auto region = serio::Region<serio::AnyAsyncDevice>::Make(*files.data,
        serio::ByteRange {
          .offset = serio::ByteOffset { 0 },
          .count = serio::ByteCount { committed },
        });
      if (!region) {
        throw std::system_error(region.error(),
          "Resource data '" + data->string()
            + "' is shorter than its committed length");
      }
      files.payloads = *region;
    }
  };

  using data::BufferResource;
  using data::PhysicsResource;
  using data::ScriptResource;
  using data::TextureResource;

  open(FilesOf<BufferResource>(), buffers_table_path_, buffers_data_path_,
    FileKind::kBuffersData);
  open(FilesOf<TextureResource>(), textures_table_path_, textures_data_path_,
    FileKind::kTexturesData);
  open(FilesOf<ScriptResource>(), scripts_table_path_, scripts_data_path_,
    FileKind::kScriptsData);
  open(FilesOf<PhysicsResource>(), physics_table_path_, physics_data_path_,
    FileKind::kPhysicsData);
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
  const auto descriptor = ReadTextureDescriptor(path);

  ValidateTextureDescriptorAgainstTable(path, descriptor);
  return descriptor.index;
}

auto LooseCookedSource::ValidateTextureDescriptorAgainstTable(
  const std::filesystem::path& path,
  const data::TextureResourceDescriptor& descriptor) const -> void
{
  const auto row_offset
    = TextureRowOffsetOrThrow(path, GetTextureTable(), descriptor.index);

  // An index-only source maps no table at mount; it maps this one to compare.
  const auto& textures = files_.at(kResourceTypeIndex<data::TextureResource>);
  if (!textures.table && !textures_table_path_) {
    throw CorruptTextureDescriptor(
      data::CookedDefect::kMissingPart, path, "has no source textures table");
  }
  const auto mapped = textures.table
    ? std::optional<serio::MappedFile> {}
    : std::optional { MapTable(*textures_table_path_) };
  const auto table_bytes = mapped ? mapped->Bytes() : textures.table->Bytes();

  ValidateTextureTableRow(path, table_bytes, row_offset, descriptor.descriptor);
}

} // namespace oxygen::content::internal
