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

namespace oxygen::content::import::internal {

//! Decodes the buffer views `EXT_meshopt_compression` compresses.
/*!
 Each compressed view is decoded from its extension's buffer into memory the
 document owns (`cgltf_buffer_view::data`, released by `cgltf_free`), which
 accessors then read in place of the view's fallback bytes. `data` has passed
 `cgltf_validate`, so every view's stride and count fit its size.

 Returns false, with a `gltf.meshopt_decode_failed` error naming the view,
 when a compressed buffer is not loaded or does not decode.
*/
[[nodiscard]] auto DecodeMeshoptBufferViews(cgltf_data& data,
  std::string_view source_id, std::vector<ImportDiagnostic>& diagnostics)
  -> bool;

} // namespace oxygen::content::import::internal
