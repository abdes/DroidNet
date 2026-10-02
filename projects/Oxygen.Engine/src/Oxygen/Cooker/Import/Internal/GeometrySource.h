//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/MaterialSlotId.h>

namespace oxygen::content::import::internal {

//! Owned geometry recipe before cooked references are linked or meshes
//! generated.
struct GeometrySource final {
  struct Bounds final {
    std::array<float, 3> min {};
    std::array<float, 3> max {};
  };

  struct Buffers final {
    std::string vertex;
    std::string index;
  };

  struct Skinned final {
    Buffers buffers;
    std::string joint_index;
    std::string joint_weight;
    std::string inverse_bind;
    std::string joint_remap;
    std::optional<std::string> skeleton;
    uint16_t joint_count = 0;
    uint16_t influences_per_vertex = 0;
    uint32_t flags = 0;
  };

  struct Procedural final {
    std::string name;
    std::vector<std::byte> parameters;
  };

  struct Submesh final {
    std::string name;
    data::MaterialSlotId slot_id {};
    std::string material;
    Bounds bounds;
    std::vector<std::string> views;
  };

  struct Lod final {
    std::string name;
    Bounds bounds;
    std::variant<Buffers, Skinned, Procedural> mesh { Buffers {} };
    std::vector<Submesh> submeshes;
  };

  std::string name;
  Bounds bounds;
  std::vector<BufferSource> buffers;
  std::vector<Lod> lods;
  std::optional<bool> content_hashing;

  OXGN_COOK_NDAPI static auto FromDescriptor(std::string_view bytes,
    const std::filesystem::path& source_path,
    std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<GeometrySource>;
};

} // namespace oxygen::content::import::internal
