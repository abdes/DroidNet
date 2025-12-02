//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <type_traits>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Renderer/api_export.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::renderer {

// NodeLookup must be a callable invocable with (const oxygen::ViewId&) and
// returning a `oxygen::scene::SceneNode` (or a compatible lightweight handle).
template <typename F>
concept NodeLookupConcept = std::invocable<F, const oxygen::ViewId&>
  && std::convertible_to<std::invoke_result_t<F, const oxygen::ViewId&>,
    oxygen::scene::SceneNode>;

class FromNodeLookup {
protected:
  OXGN_RNDR_NDAPI static auto ResolveForNode(
    oxygen::scene::SceneNode& camera_node) -> oxygen::ResolvedView;
};

template <NodeLookupConcept NodeLookup>
class SceneCameraViewResolver : public FromNodeLookup {
public:
  static_assert(std::is_invocable_r_v<oxygen::scene::SceneNode, NodeLookup,
    const oxygen::ViewId&>);

  explicit SceneCameraViewResolver(NodeLookup lookup)
    : node_lookup_(std::move(lookup))
  {
  }

  auto operator()(const oxygen::ViewId& id) const -> oxygen::ResolvedView
  {
    // Resolve the scene node from the ViewId using the provided callable.
    auto camera_node = node_lookup_(id);
    return ResolveForNode(camera_node);
  }

private:
  NodeLookup node_lookup_;
};

} // namespace oxygen::renderer
