//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

class GeometryAsset;

//! Native creation classification; internal entries remain available to tools.
enum class BuiltinGeometryAuthoringCategory : uint8_t {
  kStandard,
  kAdvanced,
  kInternal,
};

//! Stable authoring and generated-descriptor identity for a built-in shape.
struct BuiltinGeometryIdentity {
  std::string_view name;
  std::string_view generator;
  BuiltinGeometryAuthoringCategory authoring_category;
  std::string asset_uri;
  std::string descriptor_name;
};

//! All supported canonical built-in names, including internal tool resources.
OXGN_DATA_NDAPI auto GetBuiltinGeometryNames() noexcept
  -> std::span<const std::string_view>;

//! Distinguishes the built-in URI namespace from ordinary authored assets.
OXGN_DATA_NDAPI auto IsBuiltinGeometryUri(std::string_view asset_uri) noexcept
  -> bool;

//! Resolves a built-in name to its canonical identity and authoring category.
OXGN_DATA_NDAPI auto ResolveBuiltinGeometryIdentity(std::string_view asset_uri)
  -> std::optional<BuiltinGeometryIdentity>;

//! Resolves immutable geometry using shared generator/default-material
//! semantics. Repeated resolution returns the same cached asset for its
//! canonical URI. Unknown names or generation failures return null.
OXGN_DATA_NDAPI auto ResolveBuiltinGeometry(std::string_view asset_uri)
  -> std::shared_ptr<const GeometryAsset>;

} // namespace oxygen::data
