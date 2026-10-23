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
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SurfaceId.h>

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

  //! One mesh view: a buffer view selector and its local bounds.
  struct View final {
    std::string view_ref;
    //! The authored bounds, else the submesh bounds.
    Bounds bounds;
  };

  struct Submesh final {
    std::string name;
    data::SurfaceId surface_id {};
    std::string material;
    Bounds bounds;
    std::vector<View> views;
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
  //! Authored surface defaults, each of a declared surface, at most once.
  std::vector<AuthoredSurfaceDefaults> surfaces;
  //! The authored default LOD policy; absent takes the schema's default.
  std::optional<data::pak::geometry::LodPolicyRecord> lod_policy;
  std::optional<bool> content_hashing;

  //! The surfaces the submeshes declare, in first-declaration order.
  OXGN_COOK_NDAPI auto DeclaredSurfaces() const -> std::vector<data::SurfaceId>;

  OXGN_COOK_NDAPI static auto FromDescriptor(std::string_view bytes,
    const std::filesystem::path& source_path,
    std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<GeometrySource>;
};

} // namespace oxygen::content::import::internal
