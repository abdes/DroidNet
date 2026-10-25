//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ModelNodeIdentity.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Data/SceneIds.h>

namespace oxygen::content::import::internal {

namespace {
  //! `name` with the anchor's separators escaped.
  auto EscapeName(const std::string_view name) -> std::string
  {
    auto escaped = std::string {};
    escaped.reserve(name.size());
    for (const auto character : name) {
      if (character == '/' || character == '[' || character == '#'
        || character == '\\') {
        escaped.push_back('\\');
      }
      escaped.push_back(character);
    }
    return escaped;
  }

  //! Where the role of an added node's anchor starts (its unescaped `#`), or
  //! none for a source node's anchor.
  auto AddedRoleStart(const std::string_view anchor) -> std::optional<size_t>
  {
    auto escaped = false;
    auto start = std::optional<size_t> {};
    for (size_t index = 0; index < anchor.size(); ++index) {
      if (escaped) {
        escaped = false;
      } else if (anchor[index] == '\\') {
        escaped = true;
      } else if (anchor[index] == '#') {
        start = index;
      }
    }
    return start;
  }

  auto Quoted(const std::vector<std::string_view>& anchors) -> std::string
  {
    auto text = std::string {};
    for (const auto anchor : anchors) {
      if (!text.empty()) {
        text += ", ";
      }
      text += "'" + std::string(anchor) + "'";
    }
    return text;
  }
} // namespace

ModelNodeIdentity::ModelNodeIdentity(
  const Uuid source_identity, ModelImportInputs history)
  : source_identity_(source_identity.ToString())
  , history_(std::move(history))
{
}

auto ModelNodeIdentity::ForRequest(const ImportRequest& request,
  const std::string_view source_id, std::vector<ImportDiagnostic>& diagnostics)
  -> ModelNodeIdentity
{
  if (request.surface_provenance && request.model_import) {
    return ModelNodeIdentity(
      request.surface_provenance->SourceIdentity(), *request.model_import);
  }
  diagnostics.push_back({
    .severity = ImportSeverity::kWarning,
    .code = "import.source_identity_missing",
    .message = "The import has no source identity: its node IDs change on "
               "every import, and scenes cannot keep changes of its nodes",
    .source_path = std::string(source_id),
    .object_path = {},
  });
  return ModelNodeIdentity(request.surface_provenance
      ? request.surface_provenance->SourceIdentity()
      : Uuid::Generate());
}

auto ModelNodeIdentity::SourceAnchor(const SourceNode& node) -> std::string
{
  const auto escaped = EscapeName(node.name);
  const auto parent = std::string(node.parent_anchor);
  auto& count = sibling_counts_[{ parent, escaped }];
  ++count;
  auto anchor = parent.empty() ? escaped : parent + "/" + escaped;
  // A node without a name is known by its place among its unnamed siblings.
  if (count > 1U || escaped.empty()) {
    anchor += "[" + std::to_string(count) + "]";
  }
  return anchor;
}

auto ModelNodeIdentity::AddedAnchor(const AddedNode& node) -> std::string
{
  auto anchor = std::string(node.owner_anchor);
  anchor.push_back('#');
  for (const auto character : node.role) {
    anchor.push_back(
      static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return anchor;
}

auto ModelNodeIdentity::ResolveContinuity(
  const std::span<const std::string> source_anchors,
  const std::string_view source_id, std::vector<ImportDiagnostic>& diagnostics)
  -> bool
{
  continued_.clear();
  const auto& entries = history_.node_continuity;
  auto next = std::map<std::string_view, std::string_view> {};
  for (const auto& entry : entries) {
    next.emplace(entry.anchor, entry.continues);
  }
  for (const auto& entry : entries) {
    auto visited = std::set<std::string_view> { entry.anchor };
    auto at = std::string_view(entry.continues);
    while (true) {
      if (!visited.insert(at).second) {
        diagnostics.push_back({
          .severity = ImportSeverity::kError,
          .code = "import.node_continuity_cycle",
          .message = "The node continuity entries from '" + entry.anchor
            + "' form a cycle",
          .source_path = std::string(source_id),
          .object_path = entry.anchor,
        });
        return false;
      }
      const auto link = next.find(at);
      if (link == next.end()) {
        break;
      }
      at = link->second;
    }
  }

  const auto present
    = std::set<std::string_view>(source_anchors.begin(), source_anchors.end());
  auto claimed = std::set<std::string_view> {};
  const auto drop = [&](const ModelNodeContinuity& entry,
                      const std::string_view reason) -> void {
    diagnostics.push_back({
      .severity = ImportSeverity::kWarning,
      .code = "import.node_continuity_conflict",
      .message = "The node '" + entry.anchor + "' does not continue '"
        + entry.continues + "': " + std::string(reason),
      .source_path = std::string(source_id),
      .object_path = entry.anchor,
    });
  };
  for (const auto& entry : entries) {
    if (!present.contains(entry.anchor)) {
      continue;
    }
    // Follow the chain to its first anchor; a node of the file on the way
    // keeps its own ID, which no other node may take.
    auto root = std::string_view(entry.continues);
    auto in_file = present.contains(root);
    while (!in_file) {
      const auto link = next.find(root);
      if (link == next.end()) {
        break;
      }
      root = link->second;
      in_file = present.contains(root);
    }
    if (in_file) {
      drop(entry, "a node of the file has that anchor");
      continue;
    }
    if (!claimed.insert(root).second) {
      drop(entry, "another node continues it");
      continue;
    }
    continued_.emplace(entry.anchor, std::string(root));
  }
  return true;
}

auto ModelNodeIdentity::IdentityAnchor(const std::string_view anchor) const
  -> std::string
{
  if (const auto found = continued_.find(anchor); found != continued_.end()) {
    return found->second;
  }
  if (const auto role = AddedRoleStart(anchor); role.has_value()) {
    const auto owner = continued_.find(anchor.substr(0, *role));
    if (owner != continued_.end()) {
      return owner->second + std::string(anchor.substr(*role));
    }
  }
  return std::string(anchor);
}

auto ModelNodeIdentity::IdOf(const std::string_view anchor) const
  -> data::NodeId
{
  // The tag is a hash input: changing it changes every model node ID.
  const auto identity = nlohmann::json::array({
    "oxygen.model-node/v1",
    source_identity_,
    IdentityAnchor(anchor),
  });
  return data::NodeId::FromStableIdentity(identity.dump());
}

auto ModelNodeIdentity::Emit(const std::string_view anchor) -> data::NodeId
{
  const auto id = IdOf(anchor);
  published_.push_back({ .anchor = std::string(anchor), .node_id = id });
  return id;
}

auto ModelNodeIdentity::TakePublishedNodes(const std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics) -> std::vector<ModelPublishedNode>
{
  const auto& previous = history_.published_nodes;
  if (!previous.empty()) {
    auto before = std::set<std::string_view> {};
    for (const auto& node : previous) {
      before.insert(node.anchor);
    }
    auto after = std::set<std::string_view> {};
    for (const auto& node : published_) {
      after.insert(node.anchor);
    }
    auto vanished = std::vector<std::string_view> {};
    std::ranges::set_difference(before, after, std::back_inserter(vanished));
    auto added = std::vector<std::string_view> {};
    std::ranges::set_difference(after, before, std::back_inserter(added));
    if (!vanished.empty() || !added.empty()) {
      auto message
        = std::string("The model's nodes changed since its last import.");
      if (!vanished.empty()) {
        message += " Vanished: " + Quoted(vanished) + ".";
      }
      if (!added.empty()) {
        message += " New: " + Quoted(added) + ".";
      }
      diagnostics.push_back({
        .severity = ImportSeverity::kInfo,
        .code = "import.nodes_changed",
        .message = std::move(message),
        .source_path = std::string(source_id),
        .object_path = {},
      });
    }
  }
  return std::exchange(published_, {});
}

} // namespace oxygen::content::import::internal
