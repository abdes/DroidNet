//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Cooker/Import/Internal/Utils/BufferDescriptorSidecar.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <ranges>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::content::import::internal {

namespace {

  using oxygen::Format;
  using oxygen::data::pak::core::BufferResourceDesc;
  using oxygen::data::pak::core::kMaxNameSize;
  using oxygen::data::pak::core::ResourceIndexT;
  using oxygen::graphics::detail::GetFormatInfo;

  constexpr uint16_t kBufferSidecarVersion = 2;

#pragma pack(push, 1)
  struct BufferSidecarHeader final {
    char magic[4] = { 'O', 'B', 'U', 'F' };
    uint16_t version = kBufferSidecarVersion;
    uint16_t reserved = 0;
    ResourceIndexT resource_index = oxygen::data::pak::core::kNoResourceIndex;
    BufferResourceDesc descriptor {};
    uint32_t view_count = 0;
    uint32_t reserved_views = 0;
  };

  struct BufferSidecarViewEntry final {
    char name[kMaxNameSize] = {};
    uint64_t byte_offset = 0;
    uint64_t byte_length = 0;
    uint64_t element_offset = 0;
    uint64_t element_count = 0;
  };
#pragma pack(pop)

  static_assert(std::is_trivially_copyable_v<BufferSidecarHeader>);
  static_assert(std::is_trivially_copyable_v<BufferSidecarViewEntry>);

  auto AddIssue(std::vector<BufferDescriptorViewIssue>& issues,
    std::string code, std::string message, std::string object_path) -> void
  {
    issues.push_back(BufferDescriptorViewIssue {
      .code = std::move(code),
      .message = std::move(message),
      .object_path = std::move(object_path),
    });
  }

  [[nodiscard]] auto DecodeFixedName(const char* raw_name) -> std::string
  {
    size_t length = 0;
    while (length < kMaxNameSize && raw_name[length] != '\0') {
      ++length;
    }
    return std::string(raw_name, length);
  }

  [[nodiscard]] auto EncodeFixedName(const std::string_view name)
    -> std::array<char, kMaxNameSize>
  {
    auto encoded = std::array<char, kMaxNameSize> {};
    if (name.empty()) {
      return encoded;
    }
    const auto copy_len = (std::min)(name.size(), kMaxNameSize - 1U);
    std::memcpy(encoded.data(), name.data(), copy_len);
    encoded[copy_len] = '\0';
    return encoded;
  }

  [[nodiscard]] auto WillMultiplyOverflow(
    const uint64_t lhs, const uint64_t rhs) -> bool
  {
    if (lhs == 0 || rhs == 0) {
      return false;
    }
    return lhs > (std::numeric_limits<uint64_t>::max)() / rhs;
  }

  [[nodiscard]] auto EffectiveElementStride(
    const BufferResourceDesc& descriptor) -> uint64_t
  {
    if (descriptor.element_stride > 0) {
      return descriptor.element_stride;
    }
    if (descriptor.element_format == 0) {
      return 1;
    }

    const auto format = static_cast<Format>(descriptor.element_format);
    const auto& info = GetFormatInfo(format);
    if (info.bytes_per_block == 0 || info.block_size != 1) {
      return 0;
    }
    return info.bytes_per_block;
  }

  [[nodiscard]] auto BuildImplicitAllView(const BufferResourceDesc& descriptor)
    -> BufferDescriptorView
  {
    const auto stride
      = (std::max)(uint64_t { 1 }, EffectiveElementStride(descriptor));
    return BufferDescriptorView {
      .name = std::string(kImplicitBufferViewName),
      .byte_offset = 0,
      .byte_length = descriptor.size_bytes,
      .element_offset = 0,
      .element_count = descriptor.size_bytes / stride,
    };
  }

  [[nodiscard]] auto EnsureImplicitAllView(
    std::span<const BufferDescriptorView> views,
    const BufferResourceDesc& descriptor) -> std::vector<BufferDescriptorView>
  {
    auto normalized = std::vector<BufferDescriptorView> {};
    normalized.reserve(views.size() + 1U);
    normalized.push_back(BuildImplicitAllView(descriptor));

    auto seen_names = std::unordered_set<std::string> {};
    seen_names.insert(std::string(kImplicitBufferViewName));

    for (const auto& view : views) {
      if (view.name.empty() || view.name == kImplicitBufferViewName) {
        continue;
      }
      if (!seen_names.insert(view.name).second) {
        continue;
      }
      normalized.push_back(view);
    }

    return normalized;
  }

} // namespace

auto ParseBufferViewSpecs(const nlohmann::json& buffer_doc,
  const std::string_view object_path_prefix,
  std::vector<BufferDescriptorViewIssue>& issues)
  -> std::vector<BufferDescriptorViewSpec>
{
  auto specs = std::vector<BufferDescriptorViewSpec> {};
  if (!buffer_doc.contains("views")) {
    return specs;
  }

  const auto& views_json = buffer_doc.at("views");
  if (!views_json.is_array()) {
    AddIssue(issues, "buffer.view.shape_invalid", "'views' must be an array",
      std::string(object_path_prefix));
    return specs;
  }

  specs.reserve(views_json.size());
  auto seen_names = std::unordered_set<std::string> {};

  for (size_t i = 0; i < views_json.size(); ++i) {
    const auto item_path
      = std::string(object_path_prefix) + "[" + std::to_string(i) + "]";
    const auto& view_json = views_json.at(i);
    if (!view_json.is_object()) {
      AddIssue(issues, "buffer.view.shape_invalid",
        "Each view entry must be a JSON object", item_path);
      continue;
    }
    if (!view_json.contains("name") || !view_json.at("name").is_string()) {
      AddIssue(issues, "buffer.view.name_missing",
        "Each view entry must define a string 'name'", item_path + ".name");
      continue;
    }

    auto spec = BufferDescriptorViewSpec {};
    spec.name = view_json.at("name").get<std::string>();
    if (spec.name == kImplicitBufferViewName) {
      AddIssue(issues, "buffer.view.explicit_all_disallowed",
        "View name '__all__' is reserved and must not be declared explicitly",
        item_path + ".name");
      continue;
    }

    if (!seen_names.insert(spec.name).second) {
      AddIssue(issues, "buffer.view.name_duplicate",
        "Duplicate buffer view name in one descriptor", item_path + ".name");
      continue;
    }

    const auto read_u64 = [&](const std::string_view key,
                            std::optional<uint64_t>& out_value) -> bool {
      const auto key_str = std::string(key);
      if (!view_json.contains(key_str)) {
        return true;
      }
      try {
        out_value = view_json.at(key_str).get<uint64_t>();
        return true;
      } catch (const std::exception&) {
        AddIssue(issues, "buffer.view.value_invalid",
          "View field must be a non-negative integer",
          item_path + "." + std::string(key));
        return false;
      }
    };

    if (!read_u64("byte_offset", spec.byte_offset)
      || !read_u64("byte_length", spec.byte_length)
      || !read_u64("element_offset", spec.element_offset)
      || !read_u64("element_count", spec.element_count)) {
      continue;
    }

    const auto has_byte_pair
      = spec.byte_offset.has_value() && spec.byte_length.has_value();
    const auto has_element_pair
      = spec.element_offset.has_value() && spec.element_count.has_value();
    const auto has_partial_byte
      = spec.byte_offset.has_value() != spec.byte_length.has_value();
    const auto has_partial_element
      = spec.element_offset.has_value() != spec.element_count.has_value();

    if (has_partial_byte || has_partial_element
      || (has_byte_pair == has_element_pair)) {
      AddIssue(issues, "buffer.view.range_shape_invalid",
        "View must define exactly one range pair: "
        "(byte_offset, byte_length) or (element_offset, element_count)",
        item_path);
      continue;
    }

    specs.push_back(std::move(spec));
  }

  return specs;
}

auto NormalizeBufferViews(
  std::span<const BufferDescriptorViewSpec> declared_views,
  const BufferResourceDesc& descriptor,
  const std::string_view object_path_prefix,
  std::vector<BufferDescriptorViewIssue>& issues)
  -> std::vector<BufferDescriptorView>
{
  auto normalized = std::vector<BufferDescriptorView> {};
  normalized.reserve(declared_views.size() + 1U);
  normalized.push_back(BuildImplicitAllView(descriptor));

  const auto element_stride = EffectiveElementStride(descriptor);
  if (element_stride == 0) {
    AddIssue(issues, "buffer.view.element_stride_invalid",
      "Cannot compute effective element stride from buffer descriptor",
      std::string(object_path_prefix));
    return {};
  }

  for (const auto& spec : declared_views) {
    auto view = BufferDescriptorView { .name = spec.name };

    if (spec.byte_offset.has_value() && spec.byte_length.has_value()) {
      const auto byte_offset = *spec.byte_offset;
      const auto byte_length = *spec.byte_length;
      const auto byte_end = byte_offset + byte_length;
      const auto size_bytes = static_cast<uint64_t>(descriptor.size_bytes);
      if (byte_end < byte_offset || byte_end > size_bytes) {
        AddIssue(issues, "buffer.view.range_out_of_bounds",
          "Byte range exceeds buffer size", std::string(object_path_prefix));
        continue;
      }
      if ((byte_offset % element_stride) != 0
        || (byte_length % element_stride) != 0) {
        AddIssue(issues, "buffer.view.range_unaligned",
          "Byte ranges must align to effective element stride",
          std::string(object_path_prefix));
        continue;
      }

      view.byte_offset = byte_offset;
      view.byte_length = byte_length;
      view.element_offset = byte_offset / element_stride;
      view.element_count = byte_length / element_stride;
      normalized.push_back(std::move(view));
      continue;
    }

    if (spec.element_offset.has_value() && spec.element_count.has_value()) {
      const auto element_offset = *spec.element_offset;
      const auto element_count = *spec.element_count;
      if (WillMultiplyOverflow(element_offset, element_stride)
        || WillMultiplyOverflow(element_count, element_stride)) {
        AddIssue(issues, "buffer.view.range_overflow",
          "Element range overflows when converted to bytes",
          std::string(object_path_prefix));
        continue;
      }

      const auto byte_offset = element_offset * element_stride;
      const auto byte_length = element_count * element_stride;
      const auto byte_end = byte_offset + byte_length;
      const auto size_bytes = static_cast<uint64_t>(descriptor.size_bytes);
      if (byte_end < byte_offset || byte_end > size_bytes) {
        AddIssue(issues, "buffer.view.range_out_of_bounds",
          "Element range exceeds buffer size", std::string(object_path_prefix));
        continue;
      }

      view.byte_offset = byte_offset;
      view.byte_length = byte_length;
      view.element_offset = element_offset;
      view.element_count = element_count;
      normalized.push_back(std::move(view));
      continue;
    }

    AddIssue(issues, "buffer.view.range_shape_invalid",
      "View must define exactly one range pair",
      std::string(object_path_prefix));
  }

  return normalized;
}

auto SerializeBufferDescriptorSidecar(const ResourceIndexT resource_index,
  const BufferResourceDesc& descriptor,
  const std::span<const BufferDescriptorView> views) -> std::vector<std::byte>
{
  const auto final_views = EnsureImplicitAllView(views, descriptor);

  auto header = BufferSidecarHeader {};
  header.resource_index = resource_index;
  header.descriptor = descriptor;
  header.view_count = static_cast<uint32_t>(final_views.size());

  const auto payload_size = sizeof(BufferSidecarHeader)
    + final_views.size() * sizeof(BufferSidecarViewEntry);
  auto bytes = std::vector<std::byte>(payload_size);
  auto* write_ptr = bytes.data();
  std::memcpy(write_ptr, &header, sizeof(BufferSidecarHeader));
  write_ptr += sizeof(BufferSidecarHeader);

  for (const auto& view : final_views) {
    auto entry = BufferSidecarViewEntry {};
    const auto encoded_name = EncodeFixedName(view.name);
    std::memcpy(entry.name, encoded_name.data(), encoded_name.size());
    entry.byte_offset = view.byte_offset;
    entry.byte_length = view.byte_length;
    entry.element_offset = view.element_offset;
    entry.element_count = view.element_count;
    std::memcpy(write_ptr, &entry, sizeof(BufferSidecarViewEntry));
    write_ptr += sizeof(BufferSidecarViewEntry);
  }

  return bytes;
}

auto ParseBufferDescriptorSidecar(const std::span<const std::byte> bytes,
  ParsedBufferDescriptorSidecar& out, std::string& error_message) -> bool
{
  error_message.clear();
  out = ParsedBufferDescriptorSidecar {};

  if (bytes.size() < sizeof(BufferSidecarHeader)) {
    error_message = "Buffer descriptor file is truncated";
    return false;
  }

  auto header = BufferSidecarHeader {};
  std::memcpy(&header, bytes.data(), sizeof(BufferSidecarHeader));
  if (std::memcmp(header.magic, "OBUF", 4) != 0) {
    error_message = "Buffer descriptor has invalid magic";
    return false;
  }

  if (header.version != kBufferSidecarVersion) {
    error_message = "Buffer descriptor has unsupported version";
    return false;
  }

  const auto expected_size = sizeof(BufferSidecarHeader)
    + static_cast<size_t>(header.view_count) * sizeof(BufferSidecarViewEntry);
  if (bytes.size() < expected_size) {
    error_message = "Buffer descriptor view table is truncated";
    return false;
  }

  out.resource_index = header.resource_index;
  out.descriptor = header.descriptor;
  out.views.reserve(header.view_count);

  auto seen_names = std::unordered_set<std::string> {};
  const auto* read_ptr = bytes.data() + sizeof(BufferSidecarHeader);
  for (uint32_t i = 0; i < header.view_count; ++i) {
    auto entry = BufferSidecarViewEntry {};
    std::memcpy(&entry, read_ptr, sizeof(BufferSidecarViewEntry));
    read_ptr += sizeof(BufferSidecarViewEntry);

    auto name = DecodeFixedName(entry.name);
    if (name.empty()) {
      error_message = "Buffer descriptor contains an empty view name";
      return false;
    }
    if (!seen_names.insert(name).second) {
      error_message = "Buffer descriptor contains duplicate view names";
      return false;
    }

    out.views.push_back(BufferDescriptorView {
      .name = std::move(name),
      .byte_offset = entry.byte_offset,
      .byte_length = entry.byte_length,
      .element_offset = entry.element_offset,
      .element_count = entry.element_count,
    });
  }

  const auto has_implicit_all
    = std::ranges::any_of(out.views, [](const BufferDescriptorView& view) {
        return view.name == kImplicitBufferViewName;
      });
  if (!has_implicit_all) {
    error_message = "Buffer descriptor is missing required '__all__' view";
    return false;
  }

  return true;
}

} // namespace oxygen::content::import::internal
