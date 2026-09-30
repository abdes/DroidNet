//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
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
