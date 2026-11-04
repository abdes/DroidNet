//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/ModelNodeIdentity.h>
#include <Oxygen/Cooker/Import/Internal/ModelSceneNodes.h>
#include <Oxygen/Core/Transforms/Decompose.h>

namespace oxygen::content::import::internal {

namespace {

  [[nodiscard]] auto Warning(const std::string_view code, std::string message,
    const std::string_view source_id, const std::string_view object_path)
    -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kWarning,
      .code = std::string(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  //! Whether a child can hold its world transform under `parent_world`.
  [[nodiscard]] auto CanReparentUnder(const glm::mat4& parent_world) -> bool
  {
    glm::vec3 translation {};
    glm::vec3 scale { 1.0F, 1.0F, 1.0F };
    glm::quat rotation {};
    return transforms::TryDecomposeTransform(
             parent_world, translation, rotation, scale)
      && transforms::IsUniformScale(scale)
      && transforms::IsIdentityRotation(rotation);
  }

} // namespace

auto MakeSynthesizedRootNode() -> ModelNodeInput
{
  auto root = ModelNodeInput {};
  root.authored_name = "root";
  root.base_name = "root";
  root.anchor = ModelNodeIdentity::kSynthesizedRoot;
  return root;
}

auto PruneModelNodes(std::vector<ModelNodeInput> nodes,
  const NodePruningPolicy policy, const std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics) -> std::vector<ModelNodeInput>
{
  auto kept_indices = std::vector<uint32_t> {};
  kept_indices.reserve(nodes.size());
  for (uint32_t i = 0; i < nodes.size(); ++i) {
    const auto& node = nodes.at(i);
    if (policy != NodePruningPolicy::kDropEmptyNodes || node.has_renderable
      || node.has_camera || node.has_light) {
      kept_indices.push_back(i);
    }
  }

  if (kept_indices.empty()) {
    return { MakeSynthesizedRootNode() };
  }

  auto old_to_new = std::vector(nodes.size(), -1);
  for (uint32_t new_index = 0; new_index < kept_indices.size(); ++new_index) {
    old_to_new.at(kept_indices.at(new_index)) = static_cast<int32_t>(new_index);
  }

  auto pruned = std::vector<ModelNodeInput> {};
  pruned.reserve(kept_indices.size());
  for (uint32_t new_index = 0; new_index < kept_indices.size(); ++new_index) {
    auto node = nodes.at(kept_indices.at(new_index));

    auto ancestor = node.parent_index;
    while (old_to_new.at(ancestor) < 0) {
      const auto next = nodes.at(ancestor).parent_index;
      if (next == ancestor) {
        break;
      }
      ancestor = next;
    }

    const bool has_kept_ancestor = old_to_new.at(ancestor) >= 0
      && std::cmp_not_equal(old_to_new.at(ancestor), new_index);
    const bool parent_kept = ancestor == node.parent_index;

    if (!has_kept_ancestor) {
      node.parent_index = new_index;
      node.local_matrix = node.world_matrix;
    } else if (parent_kept) {
      node.parent_index = static_cast<uint32_t>(old_to_new.at(ancestor));
    } else if (const auto& parent_world = nodes.at(ancestor).world_matrix;
      !CanReparentUnder(parent_world)) {
      diagnostics.push_back(Warning(import_codes::kScenePruningReparentSkipped,
        "Skipped reparenting due to non-uniform or rotated parent; "
        "preserving world transform",
        source_id, node.base_name));
      node.parent_index = new_index;
      node.local_matrix = node.world_matrix;
    } else {
      node.parent_index = static_cast<uint32_t>(old_to_new.at(ancestor));
      constexpr auto kSingularDeterminant = 1e-6F;
      if (std::abs(glm::determinant(parent_world)) > kSingularDeterminant) {
        node.local_matrix = glm::inverse(parent_world) * node.world_matrix;
      } else {
        diagnostics.push_back(Warning(import_codes::kScenePruningSingular,
          "Node keeps its local transform: its parent transform is singular",
          source_id, node.base_name));
      }
    }

    pruned.push_back(std::move(node));
  }
  return pruned;
}

} // namespace oxygen::content::import::internal
