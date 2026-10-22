//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

//! A procedural shape the engine content pak provides.
struct EngineShape {
  //! The procedural generator, also the asset's name.
  std::string_view name;
  //! The folder under `/Engine` that holds the shape's geometry.
  std::string_view folder;
};

//! The import manifests that cook the engine content, in the order to run them.
struct EngineContentManifests {
  //! Cooks the materials, which the geometry references.
  std::filesystem::path materials;
  //! Cooks the geometry.
  std::filesystem::path geometry;
};

//! Every shape the engine content provides: the shapes authors pick from under
//! `/Engine/Generated/BasicShapes`, and engine-internal shapes under
//! `/Engine/Internal`.
OXGN_COOK_NDAPI auto GetEngineShapes() noexcept -> std::span<const EngineShape>;

//! Writes the engine content sources into `directory`: a material descriptor
//! for the default material, a procedural geometry descriptor for every engine
//! shape, and the import manifests that cook them into `cooked_root` under
//! their `/Engine` virtual paths. Descriptor values, bounds and surfaces
//! come from Oxygen.Data. Throws std::runtime_error when a shape's generator
//! cannot produce its geometry or a file cannot be written.
OXGN_COOK_NDAPI auto WriteEngineContentSources(
  const std::filesystem::path& directory,
  const std::filesystem::path& cooked_root) -> EngineContentManifests;

} // namespace oxygen::content::import
