//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

//! Parsed source facts for retaining a portable static/scalar import bundle.
struct SceneSourceInspection final {
  bool parsed = false;
  bool supported = false;
  std::string format;
  //! Decoded file paths relative to the selected source, excluding embedded
  //! data. Complete for supported sources; existence and source coherence are
  //! checked by the caller's retention transaction, before cooking captured
  //! inputs.
  std::vector<std::string> external_files;
  std::optional<double> source_unit_meters;
  std::string source_right;
  std::string source_up;
  std::string source_front;
  std::optional<bool> source_left_handed;
  bool reverses_winding = false;
  std::size_t mesh_count = 0U;
  std::size_t material_count = 0U;
  std::size_t node_count = 0U;
  std::vector<ImportDiagnostic> diagnostics;
};

//! Inspect source metadata without loading external glTF buffers or emitting.
//! The caller may inspect a private copy of the primary file before discovering
//! and coherently capturing its referenced files. Unsupported features are
//! reported using the same static/scalar policy enforced by native cooking.
OXGN_COOK_NDAPI auto InspectSceneSource(
  const std::filesystem::path& source_path,
  const std::stop_token& stop_token = {}) -> SceneSourceInspection;

//! Serialize the versioned source-inspection report for tooling clients.
OXGN_COOK_NDAPI auto ExportSceneSourceInspection(
  const SceneSourceInspection& report) -> std::string;

} // namespace oxygen::content::import
