//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import {

//! An input action an input source declares.
struct ActionSource final {
  std::string name;
  data::pak::input::InputActionValueType value_type
    = data::pak::input::InputActionValueType::kBool;
  bool consumes_input = false;
  bool cumulative = false;
  //! The action's identity: the input source's identity and the action's name.
  std::string virtual_path;
  data::AssetKey key;
};

//! An action a combo trigger completes, as authored.
struct TriggerAuxSource final {
  std::string action_name;
  uint32_t completion_states = 0;
  uint64_t time_to_complete_ns = 0;
  //! Where the entry is in the input document.
  std::string json_pointer;
};

//! A trigger of a mapping, as authored.
struct TriggerSource final {
  data::pak::input::InputTriggerType type
    = data::pak::input::InputTriggerType::kPressed;
  data::pak::input::InputTriggerBehavior behavior
    = data::pak::input::InputTriggerBehavior::kImplicit;
  float actuation_threshold = 0.5F;
  float hold_time = 0.0F;
  bool has_hold_time = false;
  float interval = 0.0F;
  bool has_interval = false;
  std::optional<std::string> linked_action_name;
  std::vector<TriggerAuxSource> aux;
  //! Where the trigger is in the input document; the shorthand `trigger` of
  //! a mapping has none and is located at the mapping.
  std::string json_pointer;
};

//! A mapping of a slot to an action, as authored.
struct MappingSource final {
  std::string action_name;
  std::string slot_name;
  std::array<float, 2> scale { 1.0F, 1.0F };
  std::array<float, 2> bias { 0.0F, 0.0F };
  std::vector<TriggerSource> triggers;
  //! Where the mapping is in the input document.
  std::string json_pointer;
};

//! A mapping context an input source declares.
struct ContextSource final {
  std::string name;
  int32_t default_priority = 0;
  std::vector<MappingSource> mappings;
  //! Where the context is in the input document.
  std::string json_pointer;
};

//! What an input source document declares, once the document is accepted.
struct InputSourceDocument final {
  std::vector<ActionSource> actions;
  std::vector<ContextSource> contexts;
  //! The key of each declared action, by its name and by its virtual path.
  std::unordered_map<std::string, data::AssetKey> local_action_keys;
};

//! Prepares an input source (`*.input.json`) from its bytes.
/*!
 The one reading of an input source document: the cook and source analysis
 both call it, so they refuse the same documents with the same codes at the
 same JSON locations. It does not touch the session, the request or a file.

 It refuses a document that is not JSON (`input.import.parse_failed`), that
 breaks the schema (`input.schema.validation_failed`,
 `input.schema.validator_failure`), and then, reporting every fault found:
 an action or a context whose name gives a virtual path the engine refuses (a
 `vpath.*` code), a type, accumulation, trigger shorthand or behavior outside
 the contract (`input.schema.contract_mismatch`), a duplicate action
 (`input.asset.action_conflict`), a duplicate context
 (`input.context.name_duplicate`), a time out of range
 (`input.context.trigger_invalid`) and a mapping, chord or combo that names a
 local action the document does not declare (`input.context.action_unresolved`).
 It warns of aux records on a trigger that is not a combo
 (`input.context.trigger_aux_ignored`) and drops them.

 A name ending in `.oiact` is a reference to an action: resolving it is left to
 the caller, which reads cooked roots. The string table and trigger count
 limits of a context depend on the build and are checked by the caller.

 @param bytes The document text.
 @param source_path The document's file, for the diagnostics.
 @param container_virtual_path The identity of the input source
   (`/Content/Input/Player.input`); each product is named under it.
 @param diagnostics Receives every diagnostic, warnings included.
 @return The document, or nothing when a diagnostic is an error.
*/
OXGN_COOK_NDAPI auto PrepareInputDocument(std::string_view bytes,
  const std::filesystem::path& source_path,
  std::string_view container_virtual_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<InputSourceDocument>;

} // namespace oxygen::content::import
