//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <optional>
#include <type_traits>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

template <typename F>
concept NodeLookupConcept = std::invocable<F, const oxygen::ViewId&>
  && std::convertible_to<std::invoke_result_t<F, const oxygen::ViewId&>,
    oxygen::scene::SceneNode>;

class FromNodeLookup {
protected:
  OXGN_VRTX_NDAPI static auto ResolveForNode(
    oxygen::scene::SceneNode& camera_node,
    std::optional<oxygen::ViewPort> viewport_override = std::nullopt)
    -> oxygen::ResolvedView;
};

template <NodeLookupConcept NodeLookup>
class SceneCameraViewResolver : public FromNodeLookup {
public:
  static_assert(std::is_invocable_r_v<oxygen::scene::SceneNode, NodeLookup,
    const oxygen::ViewId&>);

  explicit SceneCameraViewResolver(NodeLookup lookup,
    std::optional<oxygen::ViewPort> viewport_override = std::nullopt)
    : node_lookup_(std::move(lookup))
    , viewport_override_(viewport_override)
  {
  }

  auto operator()(const oxygen::ViewId& id) const -> oxygen::ResolvedView
  {
    auto camera_node = node_lookup_(id);
    return ResolveForNode(camera_node, viewport_override_);
  }

private:
  NodeLookup node_lookup_;
  std::optional<oxygen::ViewPort> viewport_override_;
};

} // namespace oxygen::vortex
