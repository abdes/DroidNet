//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportSourceObservation.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

enum class ImportDependencyKind : uint8_t {
  kAsset,
  kMaterial,
  kGeometry,
  kScene,
  kTexture,
  kBuffer,
  kScript,
  kInputAction,
  kInputMappingContext,
  kPhysicsScene,
};

struct ImportLogicalDependency final {
  std::string virtual_path;
  ImportDependencyKind kind = ImportDependencyKind::kAsset;
  std::string object_path;
  bool required = true;
};

struct ImportFileDependency final {
  std::filesystem::path path;
  bool required = true;
};

//! One scene of a glTF source.
struct ModelSourceScene final {
  size_t index = 0;
  //! The authored name; empty when the scene has none.
  std::string name;
  //! What the `gltf_scene` import setting names this scene by.
  std::string selector;
  //! Whether an import without a chosen scene takes this one.
  bool is_default = false;
  size_t root_count = 0;
  //! The distinct nodes reachable from the scene's roots.
  size_t node_count = 0;

  auto operator==(const ModelSourceScene&) const -> bool = default;
};

//! What a model source holds, from its metadata parse.
struct ModelSourceFacts final {
  //! Metres per source unit; none when the file does not state its unit.
  std::optional<double> unit_meters;
  //! Source axes as signed axis names (`+X`, `-Z`), or `unknown`.
  std::string right;
  std::string up;
  std::string front;
  //! None when the file's axes are not a valid basis.
  std::optional<bool> left_handed;
  size_t node_count = 0;
  size_t mesh_count = 0;
  //! The file's scenes; empty for a format with one root (FBX).
  std::vector<ModelSourceScene> scenes;
  //! The names the import gives the materials and textures it produces.
  std::vector<std::string> materials;
  std::vector<std::string> textures;

  auto operator==(const ModelSourceFacts&) const -> bool = default;
};

struct ImportSourceJobAnalysis final {
  std::string id;
  std::string job_type;
  std::filesystem::path source_path;
  std::vector<ImportLogicalDependency> outputs;
  std::vector<ImportLogicalDependency> references;
  std::vector<ImportFileDependency> files;
  std::vector<ImportSourceObservation> observations;
  std::vector<std::filesystem::path> accessed_paths;
  std::vector<ImportDiagnostic> diagnostics;
  //! The source facts of a model job whose source parsed.
  std::optional<ModelSourceFacts> model;
  bool complete = false;
};

//! Source discovery for one manifest batch. No cooked files are produced.
struct ImportSourceAnalysis final {
  uint32_t version = 1;
  std::string producer_version;
  std::vector<ImportSourceJobAnalysis> jobs;
  bool complete = false;

  OXGN_COOK_NDAPI auto ToJson() const -> std::string;
};

} // namespace oxygen::content::import
