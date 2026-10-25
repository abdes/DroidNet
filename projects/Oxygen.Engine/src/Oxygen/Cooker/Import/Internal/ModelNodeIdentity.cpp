//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ModelNodeIdentity.h>
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
} // namespace

ModelNodeIdentity::ModelNodeIdentity(const Uuid source_identity)
  : source_identity_(source_identity.ToString())
{
}

auto ModelNodeIdentity::ForRequest(const ImportRequest& request,
  const std::string_view source_id, std::vector<ImportDiagnostic>& diagnostics)
  -> ModelNodeIdentity
{
  if (request.surface_provenance) {
    return ModelNodeIdentity(request.surface_provenance->SourceIdentity());
  }
  diagnostics.push_back({
    .severity = ImportSeverity::kWarning,
    .code = "import.source_identity_missing",
    .message = "The import has no source identity: its node IDs change on "
               "every import, and scenes cannot keep changes of its nodes",
    .source_path = std::string(source_id),
    .object_path = {},
  });
  return ModelNodeIdentity(Uuid::Generate());
}

auto ModelNodeIdentity::SourceAnchor(const SourceNode& node) -> std::string
{
  const auto escaped = EscapeName(node.name);
  const auto parent = std::string(node.parent_anchor);
  auto& count = sibling_counts_[{ parent, escaped }];
  ++count;
  auto anchor = parent.empty() ? escaped : parent + "/" + escaped;
  if (count > 1U) {
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

auto ModelNodeIdentity::IdOf(const std::string_view anchor) const
  -> data::NodeId
{
  // The tag is a hash input: changing it changes every model node ID.
  const auto identity = nlohmann::json::array({
    "oxygen.model-node/v1",
    source_identity_,
    anchor,
  });
  return data::NodeId::FromStableIdentity(identity.dump());
}

} // namespace oxygen::content::import::internal
