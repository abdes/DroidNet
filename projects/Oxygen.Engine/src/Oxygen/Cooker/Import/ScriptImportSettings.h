//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace oxygen::content::import {

//! Tooling-facing settings for one script-asset import job.
/*!
 This DTO is intentionally string-based because it is populated from CLI and
 manifest inputs.

 It is not a runtime contract and may contain temporarily invalid values until
 validated by `BuildScriptAssetRequest()`.

 Authoritative runtime semantics are carried by:
 `ImportRequest::options.scripting`.
*/
struct ScriptAssetImportSettings final {
  //! Source script file path (CLI/manifest authored value).
  std::string source_path;
  //! Absolute loose-cooked output root (CLI/manifest authored value).
  std::string cooked_root;
  //! Optional human-readable job name.
  std::string job_name;
  //! Optional report destination path.
  std::string report_path;
  //! Verbose diagnostics toggle for tooling.
  bool verbose { false };

  //! Compile source scripts during import.
  bool compile_scripts { false };
  //! String compile mode parsed by the builder ("debug" or "optimized").
  std::string compile_mode { "debug" };
  //! String storage mode parsed by the builder ("embedded" or "external").
  std::string script_storage { "embedded" };
  //! Content hashing request (normalized by `EffectiveContentHashingEnabled`).
  bool with_content_hashing { true };
};

//! Tooling-facing settings for one scene scripting sidecar import job.
/*!
 This DTO is string-based for CLI/manifest compatibility.

 Validation and canonicalization are performed by
 `BuildScriptingSidecarRequest()` before producing an `ImportRequest`.

 Authoritative runtime semantics are carried by:
 `ImportRequest::options.scripting`.
*/
struct ScriptingSidecarImportSettings final {
  //! Sidecar source file path (CLI/manifest authored value).
  /*!
   Optional when `inline_bindings_json` is provided.
  */
  std::string source_path;
  //! Inline sidecar payload as a JSON document string.
  /*!
   Accepted inputs:
   - JSON array of binding rows (`[ ... ]`)
   - JSON object with `bindings` array (`{ "bindings": [ ... ] }`)

   Exactly one of `source_path` or `inline_bindings_json` must be set.
  */
  std::string inline_bindings_json;
  //! Absolute loose-cooked output root (CLI/manifest authored value).
  std::string cooked_root;
  //! Optional human-readable job name.
  std::string job_name;
  //! Optional report destination path.
  std::string report_path;
  //! Verbose diagnostics toggle for tooling.
  bool verbose { false };

  //! Canonical scene virtual path this sidecar targets.
  std::string target_scene_virtual_path;
  //! Content hashing request (normalized by `EffectiveContentHashingEnabled`).
  bool with_content_hashing { true };
};

} // namespace oxygen::content::import
