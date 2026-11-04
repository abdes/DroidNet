//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

//! The diagnostic codes of turning import settings into a request.
/*!
 A refusal about the settings has an empty `source_path` and a `json_pointer`
 that names the manifest key of the refused setting (`/unit_scale`);
 `ImportManifestJob` locates it in its manifest. A refusal about a document has
 that document's `source_path` and no pointer.

 Source analysis refuses a manifest's settings with the same codes, except for
 what it never looks at:

 - The cooked root: analysis has no destination, and `--cooked-root` can supply
   the one a manifest does not name.
 - How a document fails to read: analysis reads through its own snapshot and
   reports `analysis.*` codes.

 The manifest schema or manifest loading states the rest of a manifest's rules
 first, so `kCookedRootNotAbsolute`, `kContextRootNotAbsolute`,
 `kSourceMissing`, `kSourceConflict` and `kOption*` are reached from a command
 line only.
*/
namespace oxygen::content::import::request_codes {
//! A cook has no destination: no `output` and no `--cooked-root`.
inline constexpr std::string_view kCookedRootMissing
  = "import.request.cooked_root_missing";
//! A cooked root is relative.
inline constexpr std::string_view kCookedRootNotAbsolute
  = "import.request.cooked_root_not_absolute";
//! A cooked context root is relative.
inline constexpr std::string_view kContextRootNotAbsolute
  = "import.request.context_root_not_absolute";
//! A job that reads a source names none.
inline constexpr std::string_view kSourceMissing
  = "import.request.source_missing";
//! A sidecar names both or neither of a source file and inline bindings.
inline constexpr std::string_view kSourceConflict
  = "import.request.source_conflict";
//! A source's file type is not the one the job imports.
inline constexpr std::string_view kSourceFormatMismatch
  = "import.request.source_format_mismatch";
//! An input job's source is not named `*.input.json`.
inline constexpr std::string_view kInputSourceNameInvalid
  = "import.request.input_source_name_invalid";
//! A model import names no `virtual_path`.
inline constexpr std::string_view kVirtualPathMissing
  = "import.asset_virtual_path_missing";
//! A sidecar names no target scene.
inline constexpr std::string_view kTargetSceneMissing
  = "import.request.target_scene_missing";
//! A sidecar's target scene is not a canonical virtual path.
inline constexpr std::string_view kTargetSceneNotCanonical
  = "import.request.target_scene_not_canonical";
//! An option's value is none of those the option takes.
inline constexpr std::string_view kOptionInvalid
  = "import.request.option_invalid";
//! An option's number is outside the range the option takes.
inline constexpr std::string_view kOptionOutOfRange
  = "import.request.option_out_of_range";
//! `unit_scale` without `unit_policy` `custom`.
inline constexpr std::string_view kUnitScaleNeedsCustomPolicy
  = "import.request.unit_scale_needs_custom_policy";
//! `bc7_quality` without a BC7 output or data format.
inline constexpr std::string_view kBc7QualityNeedsBc7Format
  = "import.request.bc7_quality_needs_bc7_format";

//! The capture map a batch names is not a valid capture.
inline constexpr std::string_view kCapturedInputsInvalid
  = "import.request.captured_inputs_invalid";
//! The capture holds no bytes for a document.
inline constexpr std::string_view kDocumentNotCaptured
  = "import.document.not_captured";
//! A document cannot be opened or read completely.
inline constexpr std::string_view kDocumentReadFailed
  = "import.document.read_failed";
//! A document holds more bytes than the import reads from one source.
inline constexpr std::string_view kDocumentTooLarge
  = "import.document.too_large";
//! A document's bytes are not the ones the capture recorded.
inline constexpr std::string_view kDocumentCaptureMismatch
  = "import.document.capture_mismatch";
} // namespace oxygen::content::import::request_codes
