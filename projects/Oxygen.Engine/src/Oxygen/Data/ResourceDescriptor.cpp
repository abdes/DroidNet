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
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_audio.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::data {

namespace {
  template <class Desc>
  auto RequireRow(const Desc& row, const std::string_view what) -> void
  {
    if constexpr (requires { row._reserved; }) {
      if (!pak::core::IsZeroReserve(
            std::as_bytes(std::span { row._reserved }))) {
        throw CookedFormatError(CookedDefect::kReservedNotZero,
          std::string(what) + " row reserved bytes are not zero");
      }
    }
    if constexpr (std::is_same_v<Desc, pak::core::TextureResourceDesc>) {
      if (!pak::core::HasSupportedCompression(row)) {
        throw CookedFormatError(CookedDefect::kInvalidValue,
          std::string(what) + " row records a compression its format lacks");
      }
    }
    if constexpr (requires { row.content_hash; }) {
      const auto has_digest = !base::IsAllZero(row.content_hash);
      if (row.size_bytes == 0U && has_digest) {
        throw CookedFormatError(CookedDefect::kInvalidValue,
          std::string(what) + " empty row records a content digest");
      }
      if (row.size_bytes != 0U && !has_digest) {
        throw CookedFormatError(CookedDefect::kMissingDigest,
          std::string(what) + " row has no content digest");
      }
    }
  }

  auto Refusal(const CookedDefect defect, const std::string_view message)
    -> CookedFormatError
  {
    return { defect, std::string(message) };
  }

  //! Reads the header that starts `bytes` as a current descriptor of `kind`.
  auto ReadHeader(const std::span<const std::byte> bytes,
    const std::array<char, 4>& magic, const uint16_t version,
    const std::string_view kind)
    -> Result<ResourceDescriptorHeader, CookedFormatError>
  {
    const auto header = serio::CopyAt<ResourceDescriptorHeader>(bytes);
    if (!header) {
      return Err(Refusal(CookedDefect::kTooSmall,
        std::string(kind) + " descriptor is too small for its header"));
    }
    if (header->magic != magic) {
      return Err(Refusal(CookedDefect::kWrongMagic,
        std::string(kind) + " descriptor has an invalid magic"));
    }
    if (header->version != version) {
      return Err(Refusal(CookedDefect::kUnsupportedVersion,
        std::string(kind) + " descriptor has an unsupported version"));
    }
    return Ok(*header);
  }

  //! Why `views` cannot be the views of the buffer `descriptor` describes;
  //! nullopt when they can.
  /*!
   Names are non-empty and distinct, and one is `kWholeBufferViewName`, equal
   to `WholeBufferView(descriptor)`. Every other view lies in the buffer and
   on its element stride, and its element range is its byte range in
   elements.
  */
  auto CheckViews(const pak::core::BufferResourceDesc& descriptor,
    const std::span<const BufferResourceView> views)
    -> std::optional<CookedFormatError>
  {
    const auto stride = BufferElementStride(descriptor);
    if (stride == 0) {
      return Refusal(
        CookedDefect::kInvalidValue, "Buffer descriptor has no element stride");
    }
    const auto size = uint64_t { descriptor.size_bytes };
    auto names = std::unordered_set<std::string_view> {};
    for (const auto& view : views) {
      if (view.name.empty()) {
        return Refusal(CookedDefect::kInvalidValue,
          "Buffer descriptor contains an empty view name");
      }
      if (!names.insert(view.name).second) {
        return Refusal(CookedDefect::kDuplicate,
          "Buffer descriptor contains duplicate view names");
      }
      if (view.name == kWholeBufferViewName) {
        if (view != WholeBufferView(descriptor)) {
          return Refusal(CookedDefect::kInconsistent,
            "Buffer descriptor whole view does not span the buffer");
        }
        continue;
      }
      const auto in_buffer = view.byte_offset <= size
        && view.byte_length <= size - view.byte_offset;
      if (!in_buffer) {
        return Refusal(CookedDefect::kOutOfBounds,
          "Buffer descriptor view lies outside the buffer");
      }
      const auto on_stride
        = view.byte_offset % stride == 0 && view.byte_length % stride == 0;
      if (!on_stride) {
        return Refusal(CookedDefect::kMisaligned,
          "Buffer descriptor view does not start and end on its stride");
      }
      const auto elements_match_bytes
        = view.element_offset == view.byte_offset / stride
        && view.element_count == view.byte_length / stride;
      if (!elements_match_bytes) {
        return Refusal(CookedDefect::kInconsistent,
          "Buffer descriptor view does not match its element range");
      }
    }
    if (!names.contains(kWholeBufferViewName)) {
      return Refusal(CookedDefect::kMissingPart,
        std::string("Buffer descriptor is missing required '")
          + std::string(kWholeBufferViewName) + "' view");
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
  -> Result<TextureResourceDescriptor, CookedFormatError>
{
  const auto header = ReadHeader(bytes, kTextureResourceDescriptorMagic,
    kTextureResourceDescriptorVersion, "Texture");
  if (!header) {
    return Err(header.error());
  }
  if (bytes.size() != kTextureResourceDescriptorSize) {
    return Err(Refusal(CookedDefect::kSizeMismatch,
      "Texture descriptor size does not match its format"));
  }
  const auto record = serio::CopyAt<TextureResourceDescriptorRecord>(bytes);
  if (!record) {
    return Err(Refusal(
      CookedDefect::kTruncated, "Texture descriptor could not be decoded"));
  }
  return Ok(TextureResourceDescriptor {
    .index = record->header.index,
    .descriptor = record->descriptor,
  });
}

auto EncodeBufferResourceDescriptor(const BufferResourceDescriptor& descriptor)
  -> std::vector<std::byte>
{
  if (const auto error = CheckViews(descriptor.descriptor, descriptor.views)) {
    throw std::invalid_argument(error->what());
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
  -> Result<BufferResourceDescriptor, CookedFormatError>
{
  if (const auto header = ReadHeader(bytes, kBufferResourceDescriptorMagic,
        kBufferResourceDescriptorVersion, "Buffer");
    !header) {
    return Err(header.error());
  }
  serio::ReadOnlyMemoryStream stream(bytes);
  serio::Reader reader(stream);
  auto record = BufferResourceDescriptorRecord {};
  if (!reader.ReadInto(record)) {
    return Err(
      Refusal(CookedDefect::kTruncated, "Buffer descriptor is truncated"));
  }
  const auto views_size
    = uint64_t { record.view_count } * sizeof(BufferViewRecord);
  if (bytes.size()
    != sizeof(record) + views_size + uint64_t { record.strings_size }) {
    return Err(Refusal(CookedDefect::kSizeMismatch,
      "Buffer descriptor size does not match its view and string counts"));
  }

  auto views = std::vector<BufferViewRecord>(record.view_count);
  if (!reader.ReadBlobInto(std::as_writable_bytes(std::span { views }))) {
    return Err(Refusal(
      CookedDefect::kTruncated, "Buffer descriptor view table is truncated"));
  }
  auto strings = DescriptorStrings {};
  try {
    const auto table = bytes.subspan(sizeof(record) + views_size);
    auto chars = std::vector<char>(table.size());
    std::ranges::transform(table, chars.begin(),
      [](const std::byte value) -> char { return static_cast<char>(value); });
    strings = DescriptorStrings { std::move(chars) };
  } catch (const std::invalid_argument&) {
    return Err(Refusal(CookedDefect::kInvalidString,
      "Buffer descriptor string table is invalid"));
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
      return Err(Refusal(CookedDefect::kInvalidString,
        "Buffer descriptor view name is not in its table"));
    }
    result.views.push_back(BufferResourceView {
      .name = std::string(*name),
      .byte_offset = view.byte_offset,
      .byte_length = view.byte_length,
      .element_offset = view.element_offset,
      .element_count = view.element_count,
    });
  }
  if (auto error = CheckViews(result.descriptor, result.views)) {
    return Err(std::move(*error));
  }
  return Ok(std::move(result));
}

auto RequireResourceRow(const pak::core::TextureResourceDesc& row,
  const std::string_view what) -> void
{
  RequireRow(row, what);
}

auto RequireResourceRow(
  const pak::core::BufferResourceDesc& row, const std::string_view what) -> void
{
  RequireRow(row, what);
}

auto RequireResourceRow(
  const pak::audio::AudioResourceDesc& row, const std::string_view what) -> void
{
  RequireRow(row, what);
}

auto RequireResourceRow(const pak::physics::PhysicsResourceDesc& row,
  const std::string_view what) -> void
{
  RequireRow(row, what);
}

auto RequireResourceRow(const pak::scripting::ScriptResourceDesc& row,
  const std::string_view what) -> void
{
  RequireRow(row, what);
}

} // namespace oxygen::data
