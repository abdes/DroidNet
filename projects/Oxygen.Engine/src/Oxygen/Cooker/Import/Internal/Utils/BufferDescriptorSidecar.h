//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Record.h>

namespace oxygen::content::import::internal {

inline constexpr std::string_view kImplicitBufferViewName = "__all__";

//! The `.obuf` sidecar: this header, `view_count` view entries, then a string
//! table of `strings_size` bytes.
inline constexpr std::array kBufferSidecarMagic { 'O', 'B', 'U', 'F' };
inline constexpr uint16_t kBufferSidecarVersion = 4;

#pragma pack(push, 1)
struct BufferSidecarHeader final {
  std::array<char, 4> magic = kBufferSidecarMagic;
  uint16_t version = kBufferSidecarVersion;
  uint16_t reserved = 0;
  data::pak::core::ResourceIndexT resource_index
    = data::pak::core::kNoResourceIndex;
  data::pak::core::BufferResourceDesc descriptor {};
  uint32_t view_count = 0;
  //! Size of the string table that follows the view entries.
  uint32_t strings_size = 0;
};

struct BufferSidecarViewEntry final {
  //! View name, in the string table that follows the view entries.
  uint32_t name_offset = 0;
  uint64_t byte_offset = 0;
  uint64_t byte_length = 0;
  uint64_t element_offset = 0;
  uint64_t element_count = 0;
};
#pragma pack(pop)

struct BufferDescriptorViewSpec final {
  std::string name;
  std::optional<uint64_t> byte_offset;
  std::optional<uint64_t> byte_length;
  std::optional<uint64_t> element_offset;
  std::optional<uint64_t> element_count;
};

struct BufferDescriptorView final {
  std::string name;
  uint64_t byte_offset = 0;
  uint64_t byte_length = 0;
  uint64_t element_offset = 0;
  uint64_t element_count = 0;
};

struct BufferDescriptorViewIssue final {
  std::string code;
  std::string message;
  std::string object_path;
};

struct ParsedBufferDescriptorSidecar final {
  data::pak::core::ResourceIndexT resource_index
    = data::pak::core::kNoResourceIndex;
  data::pak::core::BufferResourceDesc descriptor {};
  std::vector<BufferDescriptorView> views;
};

[[nodiscard]] auto ParseBufferViewSpecs(const nlohmann::json& buffer_doc,
  std::string_view object_path_prefix,
  std::vector<BufferDescriptorViewIssue>& issues)
  -> std::vector<BufferDescriptorViewSpec>;

[[nodiscard]] auto NormalizeBufferViews(
  std::span<const BufferDescriptorViewSpec> declared_views,
  const data::pak::core::BufferResourceDesc& descriptor,
  std::string_view object_path_prefix,
  std::vector<BufferDescriptorViewIssue>& issues)
  -> std::vector<BufferDescriptorView>;

[[nodiscard]] auto SerializeBufferDescriptorSidecar(
  data::pak::core::ResourceIndexT resource_index,
  const data::pak::core::BufferResourceDesc& descriptor,
  std::span<const BufferDescriptorView> views) -> std::vector<std::byte>;

[[nodiscard]] auto ParseBufferDescriptorSidecar(
  std::span<const std::byte> bytes, ParsedBufferDescriptorSidecar& out,
  std::string& error_message) -> bool;

} // namespace oxygen::content::import::internal

// clang-format off
namespace oxygen::serio {
template <> inline constexpr bool kIsRecord<content::import::internal::BufferSidecarHeader> = true;
template <> inline constexpr bool kIsRecord<content::import::internal::BufferSidecarViewEntry> = true;
} // namespace oxygen::serio
// clang-format on
