//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::data {

namespace {

  //! Why `header` does not start a current descriptor of `kind`; nullopt when
  //! it does.
  auto CheckHeader(const ResourceDescriptorHeader& header,
    const std::array<char, 4>& magic, const uint16_t version,
    const std::string_view kind) -> std::optional<std::string>
  {
    if (header.magic != magic) {
      return std::string(kind) + " descriptor has an invalid magic";
    }
    if (header.version != version) {
      return std::string(kind) + " descriptor has an unsupported version";
    }
    return std::nullopt;
  }

  //! Why `views` cannot be a buffer descriptor's views; nullopt when they can.
  auto CheckViewNames(const std::span<const BufferResourceView> views)
    -> std::optional<std::string>
  {
    auto names = std::unordered_set<std::string_view> {};
    for (const auto& view : views) {
      if (view.name.empty()) {
        return std::string("Buffer descriptor contains an empty view name");
      }
      if (!names.insert(view.name).second) {
        return std::string("Buffer descriptor contains duplicate view names");
      }
    }
    if (!names.contains(kWholeBufferViewName)) {
      return std::string("Buffer descriptor is missing required '")
        + std::string(kWholeBufferViewName) + "' view";
    }
    return std::nullopt;
  }

  auto ToBytes(const serio::MemoryStream& stream) -> std::vector<std::byte>
  {
    const auto bytes = stream.Data();
    return { bytes.begin(), bytes.end() };
  }

} // namespace

auto BufferElementStride(const pak::core::BufferResourceDesc& descriptor)
  -> uint64_t
{
  if (descriptor.element_stride > 0) {
    return descriptor.element_stride;
  }
  if (descriptor.element_format == 0) {
    return 1;
  }
  const auto& info = graphics::detail::GetFormatInfo(
    static_cast<Format>(descriptor.element_format));
  if (info.bytes_per_block == 0 || info.block_size != 1) {
    return 0;
  }
  return info.bytes_per_block;
}

auto WholeBufferView(const pak::core::BufferResourceDesc& descriptor)
  -> BufferResourceView
{
  const auto stride = std::max(uint64_t { 1 }, BufferElementStride(descriptor));
  return BufferResourceView {
    .name = std::string(kWholeBufferViewName),
    .byte_offset = 0,
    .byte_length = descriptor.size_bytes,
    .element_offset = 0,
    .element_count = descriptor.size_bytes / stride,
  };
}

auto EncodeTextureResourceDescriptor(
  const TextureResourceDescriptor& descriptor) -> std::vector<std::byte>
{
  const auto record = TextureResourceDescriptorRecord {
    .header = {
      .magic = kTextureResourceDescriptorMagic,
      .index = descriptor.index,
      .version = kTextureResourceDescriptorVersion,
    },
    .descriptor = descriptor.descriptor,
  };
  serio::MemoryStream stream;
  serio::Writer writer(stream);
  if (!writer.Write(record)) {
    throw std::runtime_error("Texture descriptor could not be encoded");
  }
  return ToBytes(stream);
}

auto DecodeTextureResourceDescriptor(const std::span<const std::byte> bytes)
  -> Result<TextureResourceDescriptor, std::string>
{
  if (bytes.size() != kTextureResourceDescriptorSize) {
    return Err(
      std::string("Texture descriptor size does not match the current format"));
  }
  serio::ReadOnlyMemoryStream stream(bytes);
  serio::Reader reader(stream);
  auto record = TextureResourceDescriptorRecord {};
  if (!reader.ReadInto(record)) {
    return Err(std::string("Texture descriptor could not be decoded"));
  }
  if (auto error = CheckHeader(record.header, kTextureResourceDescriptorMagic,
        kTextureResourceDescriptorVersion, "Texture")) {
    return Err(std::move(*error));
  }
  return Ok(TextureResourceDescriptor {
    .index = record.header.index,
    .descriptor = record.descriptor,
  });
}

auto EncodeBufferResourceDescriptor(const BufferResourceDescriptor& descriptor)
  -> std::vector<std::byte>
{
  if (auto error = CheckViewNames(descriptor.views)) {
    throw std::invalid_argument(*error);
  }

  auto strings = DescriptorStringsBuilder {};
  auto views = std::vector<BufferViewRecord> {};
  views.reserve(descriptor.views.size());
  for (const auto& view : descriptor.views) {
    views.push_back(BufferViewRecord {
      .byte_offset = view.byte_offset,
      .byte_length = view.byte_length,
      .element_offset = view.element_offset,
      .element_count = view.element_count,
      .name_offset = strings.Add(view.name),
    });
  }

  const auto record = BufferResourceDescriptorRecord {
    .header = {
      .magic = kBufferResourceDescriptorMagic,
      .index = descriptor.index,
      .version = kBufferResourceDescriptorVersion,
    },
    .descriptor = descriptor.descriptor,
    .view_count = static_cast<uint32_t>(views.size()),
    .strings_size = strings.Size(),
  };
  serio::MemoryStream stream;
  serio::Writer writer(stream);
  if (!writer.Write(record)
    || !writer.WriteBlob(std::as_bytes(std::span { views }))
    || !writer.WriteBlob(strings.Bytes())) {
    throw std::runtime_error("Buffer descriptor could not be encoded");
  }
  return ToBytes(stream);
}

auto DecodeBufferResourceDescriptor(const std::span<const std::byte> bytes)
  -> Result<BufferResourceDescriptor, std::string>
{
  serio::ReadOnlyMemoryStream stream(bytes);
  serio::Reader reader(stream);
  auto record = BufferResourceDescriptorRecord {};
  if (!reader.ReadInto(record)) {
    return Err(std::string("Buffer descriptor is truncated"));
  }
  if (auto error = CheckHeader(record.header, kBufferResourceDescriptorMagic,
        kBufferResourceDescriptorVersion, "Buffer")) {
    return Err(std::move(*error));
  }
  const auto views_size
    = uint64_t { record.view_count } * sizeof(BufferViewRecord);
  if (bytes.size()
    != sizeof(record) + views_size + uint64_t { record.strings_size }) {
    return Err(std::string(
      "Buffer descriptor size does not match its view and string counts"));
  }

  auto views = std::vector<BufferViewRecord>(record.view_count);
  if (!reader.ReadBlobInto(std::as_writable_bytes(std::span { views }))) {
    return Err(std::string("Buffer descriptor view table is truncated"));
  }
  auto strings = DescriptorStrings {};
  try {
    const auto table = bytes.subspan(sizeof(record) + views_size);
    auto chars = std::vector<char>(table.size());
    std::ranges::transform(table, chars.begin(),
      [](const std::byte value) -> char { return static_cast<char>(value); });
    strings = DescriptorStrings { std::move(chars) };
  } catch (const std::invalid_argument&) {
    return Err(std::string("Buffer descriptor string table is invalid"));
  }

  auto result = BufferResourceDescriptor {
    .index = record.header.index,
    .descriptor = record.descriptor,
    .views = {},
  };
  result.views.reserve(views.size());
  for (const auto& view : views) {
    const auto name = strings.TryGet(view.name_offset);
    if (!name) {
      return Err(
        std::string("Buffer descriptor view name is not in its table"));
    }
    result.views.push_back(BufferResourceView {
      .name = std::string(*name),
      .byte_offset = view.byte_offset,
      .byte_length = view.byte_length,
      .element_offset = view.element_offset,
      .element_count = view.element_count,
    });
  }
  if (auto error = CheckViewNames(result.views)) {
    return Err(std::move(*error));
  }
  return Ok(std::move(result));
}

} // namespace oxygen::data
