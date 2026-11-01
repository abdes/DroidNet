//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/Internal/Utils/BufferViews.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/ResourceDescriptor.h>

namespace oxygen::content::import::internal {

namespace {

  using oxygen::data::BufferResourceView;
  using oxygen::data::pak::core::BufferResourceDesc;

  auto AddIssue(std::vector<BufferDescriptorViewIssue>& issues,
    std::string code, std::string message, std::string object_path) -> void
  {
    issues.push_back(BufferDescriptorViewIssue {
      .code = std::move(code),
      .message = std::move(message),
      .object_path = std::move(object_path),
    });
  }

  [[nodiscard]] auto WillMultiplyOverflow(
    const uint64_t lhs, const uint64_t rhs) -> bool
  {
    if (lhs == 0 || rhs == 0) {
      return false;
    }
    return lhs > std::numeric_limits<uint64_t>::max() / rhs;
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
    if (spec.name == data::kWholeBufferViewName) {
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
  -> std::vector<BufferResourceView>
{
  auto normalized = std::vector<BufferResourceView> {};
  normalized.reserve(declared_views.size() + 1U);
  normalized.push_back(data::WholeBufferView(descriptor));

  const auto element_stride = data::BufferElementStride(descriptor);
  if (element_stride == 0) {
    AddIssue(issues, "buffer.view.element_stride_invalid",
      "Cannot compute effective element stride from buffer descriptor",
      std::string(object_path_prefix));
    return {};
  }

  for (const auto& spec : declared_views) {
    auto view = BufferResourceView { .name = spec.name };

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

auto WithWholeBufferView(const BufferResourceDesc& descriptor,
  const std::span<const BufferResourceView> views)
  -> std::vector<BufferResourceView>
{
  auto normalized = std::vector<BufferResourceView> {};
  normalized.reserve(views.size() + 1U);
  normalized.push_back(data::WholeBufferView(descriptor));

  auto seen_names = std::unordered_set<std::string> {};
  seen_names.insert(std::string(data::kWholeBufferViewName));

  for (const auto& view : views) {
    if (view.name.empty() || view.name == data::kWholeBufferViewName) {
      continue;
    }
    if (!seen_names.insert(view.name).second) {
      continue;
    }
    normalized.push_back(view);
  }

  return normalized;
}

} // namespace oxygen::content::import::internal
