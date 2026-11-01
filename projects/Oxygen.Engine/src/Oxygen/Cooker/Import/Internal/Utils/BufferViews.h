//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/ResourceDescriptor.h>

namespace oxygen::content::import::internal {

//! A view as a buffer container declares it: one range pair, in bytes or in
//! elements.
struct BufferDescriptorViewSpec final {
  std::string name;
  std::optional<uint64_t> byte_offset;
  std::optional<uint64_t> byte_length;
  std::optional<uint64_t> element_offset;
  std::optional<uint64_t> element_count;
};

struct BufferDescriptorViewIssue final {
  std::string code;
  std::string message;
  std::string object_path;
};

[[nodiscard]] auto ParseBufferViewSpecs(const nlohmann::json& buffer_doc,
  std::string_view object_path_prefix,
  std::vector<BufferDescriptorViewIssue>& issues)
  -> std::vector<BufferDescriptorViewSpec>;

//! Resolves declared views against the buffer, after its whole-buffer view.
[[nodiscard]] auto NormalizeBufferViews(
  std::span<const BufferDescriptorViewSpec> declared_views,
  const data::pak::core::BufferResourceDesc& descriptor,
  std::string_view object_path_prefix,
  std::vector<BufferDescriptorViewIssue>& issues)
  -> std::vector<data::BufferResourceView>;

//! The views a buffer descriptor stores: the whole-buffer view, then `views`
//! without unnamed, whole-buffer or repeated names.
[[nodiscard]] auto WithWholeBufferView(
  const data::pak::core::BufferResourceDesc& descriptor,
  std::span<const data::BufferResourceView> views)
  -> std::vector<data::BufferResourceView>;

} // namespace oxygen::content::import::internal
