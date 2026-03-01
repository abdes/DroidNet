//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace oxygen::content::import {

//! Runtime settings for physics sidecar import requests.
/*!
 This is the authoritative domain payload consumed by physics-sidecar
 job/pipeline routing.
*/
struct PhysicsImportSettings final {
  //! Canonical scene virtual path this sidecar targets.
  std::string target_scene_virtual_path;

  //! Inline sidecar JSON payload normalized as:
  //! `{ "bindings": { ... } }`.
  std::string inline_bindings_json;
};

//! Tooling-facing settings for one scene physics sidecar import job.
/*!
 This DTO is string-based for CLI/manifest compatibility.

 Validation and canonicalization are performed by
 `BuildPhysicsSidecarRequest()` before producing an `ImportRequest`.

 Authoritative runtime semantics are carried by:
 `ImportRequest::physics`.
*/
struct PhysicsSidecarImportSettings final {
  //! Sidecar source file path (CLI/manifest authored value).
  /*!
   Optional when `inline_bindings_json` is provided.
  */
  std::string source_path;

  //! Inline sidecar payload as a JSON document string.
  /*!
   Accepted inputs:
   - JSON object with `bindings` object (`{ "bindings": { ... } }`)

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
