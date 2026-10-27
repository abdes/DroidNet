//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/SceneIds.h>

namespace oxygen::content::import::internal {

//! The anchors and IDs of a model's nodes.
/*!
 A node's anchor is the path of names in the source file's own hierarchy, from
 the model's roots to the node, before node pruning: `Body/Door_FL`. Same-named
 siblings are numbered in file order from the second (`Body/Mirror`,
 `Body/Mirror[2]`), and unnamed ones from the first (`Body/[1]`). A node the
 importer adds appends its role to its owner's anchor
 (`Body/Headlight_L#light`); a root the importer synthesizes is `#root`. Within
 a name, `/`, `[`, `#` and `\` are escaped with `\`.

 A node's ID is derived from the model's source identity and its anchor, never
 from where the model lives, so it is the same on every import and after the
 model moves.

 The model import record's node continuity makes a node keep the ID of an
 earlier anchor: a renamed or moved node continues the anchor it had. Chains
 are followed to their first anchor, and a node the importer adds follows its
 owner. An entry that would give two nodes one ID is dropped and reported.
*/
class ModelNodeIdentity final {
public:
  //! The anchor of a root the importer synthesizes.
  static constexpr std::string_view kSynthesizedRoot = "#root";

  //! A node of the source file's hierarchy.
  struct SourceNode final {
    //! The anchor of the node's parent; empty for one of the model's roots.
    std::string_view parent_anchor;
    //! The node's name in the source file.
    std::string_view name;
  };

  //! A node the importer adds to a node of the source file.
  struct AddedNode final {
    //! The anchor of the node it is added to.
    std::string_view owner_anchor;
    //! What the node is for, such as `Light`.
    std::string_view role;
  };

  //! Derives IDs under `source_identity`; `history` is what the model's
  //! import record established.
  OXGN_COOK_API explicit ModelNodeIdentity(
    Uuid source_identity, ModelImportInputs history = {});

  //! The identity of the import's model, from its model import record. An
  //! import without a record is reported as `import.source_identity_missing`,
  //! since its identity, and so its node IDs, change on every import.
  OXGN_COOK_NDAPI static auto ForRequest(const ImportRequest& request,
    std::string_view source_id, std::vector<ImportDiagnostic>& diagnostics)
    -> ModelNodeIdentity;

  //! The anchor of `node`, the next of its parent's children with its name.
  OXGN_COOK_NDAPI auto SourceAnchor(const SourceNode& node) -> std::string;

  //! The anchor of `node`, which spells its role in lowercase (`Light` gives
  //! `#light`).
  OXGN_COOK_NDAPI static auto AddedAnchor(const AddedNode& node) -> std::string;

  //! Applies the record's node continuity, given every anchor of the source
  //! file's hierarchy. Reports each entry it drops as
  //! `import.node_continuity_conflict` (a warning); returns false, with
  //! `import.node_continuity_cycle` (an error), when entries form a cycle.
  OXGN_COOK_NDAPI auto ResolveContinuity(
    std::span<const std::string> source_anchors, std::string_view source_id,
    std::vector<ImportDiagnostic>& diagnostics) -> bool;

  //! The ID of the node with `anchor`.
  OXGN_COOK_NDAPI auto IdOf(std::string_view anchor) const -> data::NodeId;

  //! The ID of the node with `anchor`, which the import emits.
  OXGN_COOK_NDAPI auto Emit(std::string_view anchor) -> data::NodeId;

  //! The nodes emitted, in emission order, with those whose anchor the
  //! previous publication did not emit and those earlier publications emitted
  //! whose ID this import did not. Reports, against the previous
  //! publication's nodes, the anchors that vanished and those that are new
  //! (`import.nodes_changed`, info).
  OXGN_COOK_NDAPI auto TakePublishedNodes(std::string_view source_id,
    std::vector<ImportDiagnostic>& diagnostics) -> ModelNodePublication;

private:
  //! The anchor whose ID the node with `anchor` has.
  [[nodiscard]] auto IdentityAnchor(std::string_view anchor) const
    -> std::string;

  std::string source_identity_;
  ModelImportInputs history_;
  //! The names already used under each parent anchor, with their count.
  std::map<std::pair<std::string, std::string>, uint32_t> sibling_counts_;
  //! Each continuing anchor of the import, with the anchor whose ID it has.
  std::map<std::string, std::string, std::less<>> continued_;
  std::vector<ModelPublishedNode> published_;
};

} // namespace oxygen::content::import::internal
