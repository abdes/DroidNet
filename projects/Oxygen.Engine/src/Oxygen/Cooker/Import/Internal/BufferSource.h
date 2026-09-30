//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferDescriptorSidecar.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

//! A raw input and the buffer resource it declares, before destination linking.
struct BufferSource final {
  std::filesystem::path source_path;
  std::string source_id;
  std::string object_path;
  std::vector<internal::BufferDescriptorViewSpec> view_specs;
  uint32_t usage_flags = 0;
  uint32_t element_stride = 1;
  uint8_t element_format = 0;
  uint64_t alignment = 16;
  uint64_t content_hash = 0;

  //! Prepare authored buffer declarations without reading or emitting files.
  OXGN_COOK_NDAPI static auto FromDeclarations(
    const nlohmann::json& buffer_chunks,
    const std::filesystem::path& descriptor_path,
    std::vector<ImportDiagnostic>& diagnostics,
    std::string_view object_path_prefix = "buffers")
    -> std::optional<std::vector<BufferSource>>;
};

} // namespace oxygen::content::import::internal
