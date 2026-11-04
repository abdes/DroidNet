//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/gltf/cgltf.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import::internal {

//! Refuses a document whose declared sizes do not hold, before anything reads
//! or allocates by them.
/*!
 Every number here comes from the file. `cgltf_validate` computes the same
 extents with arithmetic that wraps, and reads sparse indices before it has
 checked their view, so this runs first:

 - A buffer view lies inside its buffer.
 - An accessor's elements lie inside its view; its sparse indices and values
   lie inside theirs, and there are no more of them than it has elements.
 - An accessor has at most 2^32-1 elements, the most a mesh can index.
 - An accessor with no view, which reads as zeros, and a compressed view, which
   decodes to memory, take no more bytes than `limit`: nothing in the file
   backs them.

 Returns false, with a `gltf.validation_failed` error naming the object, the
 size it declares and the room it has.
*/
[[nodiscard]] auto CheckDeclaredExtents(const cgltf_data& data,
  serio::ByteCount limit, std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics) -> bool;

//! Makes the document's text what it says, in UTF-8; runs before anything reads
//! it, buffers included.
/*!
 cgltf keeps strings as the JSON spells them, so each one's escapes are decoded
 here: `Café` is `Café`, and `textures\/a.png` a path. Then the bytes that
 are not UTF-8 are replaced with `_`, whether the JSON held them raw or as an
 escaped surrogate, since cgltf encodes each half of a pair alone. Reports and
 hash inputs are JSON, which refuses such bytes. Names, URIs and the extension
 lists are covered, with one `gltf.text_not_utf8` warning for the document.
*/
auto NormalizeDocumentText(cgltf_data& data, std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics) -> void;

//! Refuses a node hierarchy deeper than `kMaxModelNodeDepth`.
/*!
 Measures the longest path through `children`. Returns false with
 `import.model_node_depth_exceeded`, or with `gltf.validation_failed` for a
 cycle, which a scene's roots need not reach.
*/
[[nodiscard]] auto CheckNodeDepth(const cgltf_data& data,
  std::string_view source_id, std::vector<ImportDiagnostic>& diagnostics)
  -> bool;

} // namespace oxygen::content::import::internal
