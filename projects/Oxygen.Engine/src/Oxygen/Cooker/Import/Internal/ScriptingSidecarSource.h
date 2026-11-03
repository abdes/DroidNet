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
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SceneIds.h>

namespace oxygen::content::import {

//! A script parameter with its strings; the record's string offsets are
//! assigned when it is written into a descriptor's string table.
struct ScriptingSidecarParam final {
  data::pak::scripting::ScriptParamRecord record = {};
  std::string key;
  std::string string_value;
  std::string asset_virtual_path;
  //! The node path of a node value naming a node an instance brings in; its
  //! index is assigned when the record is written with the scene's paths.
  std::vector<data::NodeId> node_path;
};

//! One parsed `bindings` row.
struct ScriptingSidecarBindingRow final {
  uint32_t node_index = 0;
  data::ScriptSlotId slot_id;
  std::string script_virtual_path;
  int32_t execution_order = 0;
  std::vector<ScriptingSidecarParam> params;
  //! The row's index in the sidecar's `bindings`, which its diagnostics name.
  size_t source_index = 0;
};

//! Changes of the parameters of one slot of a node an instance brings in.
struct ScriptingSidecarInstanceRow final {
  uint32_t node_index = 0;
  std::vector<data::NodeId> node_path;
  data::ScriptSlotId slot_id;
  std::vector<ScriptingSidecarParam> params;
  //! The row's index in the sidecar's `instance_bindings`, which its
  //! diagnostics name.
  size_t source_index = 0;
};

//! The rows of a scripting sidecar, sorted by their identity.
struct ScriptingSidecarDocument final {
  std::vector<ScriptingSidecarBindingRow> rows;
  std::vector<ScriptingSidecarInstanceRow> instance_rows;
};

//! Validates and interprets the bytes of a scripting sidecar; no scene, script
//! or cooked-resource lookups.
/*!
 Accepts an object with a `bindings` array, and a bare array of rows, which is
 read as `{ "bindings": [...] }`. Checks that depend on the target scene, the
 scripts or their declarations are left to the import.

 @return The document, or nothing when an error was added to `diagnostics`.
*/
OXGN_COOK_NDAPI auto PrepareScriptingSidecarDocument(std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<ScriptingSidecarDocument>;

//! The text of the sidecar `text` holds, in the object form with a `bindings`
//! array, or nothing when `text` is not valid JSON.
OXGN_COOK_NDAPI auto NormalizeScriptingSidecarText(std::string_view text)
  -> std::optional<std::string>;

} // namespace oxygen::content::import
