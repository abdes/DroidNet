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

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferViews.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

//! A raw input and the buffer resource it declares, before destination linking.
struct BufferSource final {
  std::filesystem::path source_path;
  //! The product's virtual path, resolved against the container's identity.
  std::string source_id;
  //! Where the declaration is in the document named by `source_path`.
  std::string json_pointer;
  std::vector<internal::BufferDescriptorViewSpec> view_specs;
  uint32_t usage_flags = 0;
  uint32_t element_stride = 1;
  uint8_t element_format = 0;
  uint64_t alignment = 16;

  //! Prepare a buffer container document: parse it, validate the whole
  //! document against its schema, then prepare its `buffers`.
  /*!
   Refuses, adding the error, an invalid or non-object document
   (`buffer.container.request_invalid_json`) or one the schema refuses
   (`buffer.container.schema_validation_failed`); in the latter case the
   declarations are not prepared again, so no issue is reported twice.
   `anchor` and `mounts` are those of `FromDeclarations`.
  */
  OXGN_COOK_NDAPI static auto FromContainer(std::string_view bytes,
    const std::filesystem::path& descriptor_path, std::string_view anchor,
    const MountTable& mounts, std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<std::vector<BufferSource>>;

  //! Prepare authored buffer declarations without reading or emitting files.
  /*!
   Each `source` is a mount path, or a path relative to the descriptor that
   stays in its mount, located through `mounts`
   (`MountTable::LocateReference`). Each `virtual_path` names a product
   relative to `anchor`, the identity of the container or geometry that
   declares it, so a product lives in the container's mount; `source_id` is
   the resolved path, and two declarations resolving to one path are refused.
  */
  OXGN_COOK_NDAPI static auto FromDeclarations(
    const nlohmann::json& buffer_chunks,
    const std::filesystem::path& descriptor_path, std::string_view anchor,
    const MountTable& mounts, std::vector<ImportDiagnostic>& diagnostics,
    const nlohmann::json::json_pointer& buffers_pointer
    = nlohmann::json::json_pointer { "/buffers" })
    -> std::optional<std::vector<BufferSource>>;
};

} // namespace oxygen::content::import::internal
