//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>

namespace oxygen::content::import::internal {

//! A source node on its way to the scene, as either model format reads it.
/*!
 A root is its own parent. `source_node` is the format's own node, for the
 adapter that read it.
*/
struct ModelNodeInput final {
  std::string authored_name;
  std::string base_name;
  //! The node's anchor, its model node identity (ModelNodeIdentity).
  std::string anchor;
  uint32_t parent_index = 0;
  glm::mat4 local_matrix { 1.0F };
  glm::mat4 world_matrix { 1.0F };
  bool has_renderable = false;
  bool has_camera = false;
  bool has_light = false;
  bool visible = true;
  const void* source_node = nullptr;
};

//! The root the importer adds to a model with no node to keep.
[[nodiscard]] auto MakeSynthesizedRootNode() -> ModelNodeInput;

//! The nodes `policy` keeps, each still where the source places it.
/*!
 A kept node under a dropped one moves to its nearest kept ancestor, or becomes
 a root at its world transform when it has none. A kept ancestor that is rotated
 or scaled unevenly could not reproduce the node's transform, so the node
 becomes a root with a warning instead. A model left with nothing keeps one
 synthesized root.
*/
[[nodiscard]] auto PruneModelNodes(std::vector<ModelNodeInput> nodes,
  NodePruningPolicy policy, std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics) -> std::vector<ModelNodeInput>;

} // namespace oxygen::content::import::internal
