//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Renderer/Types/PositionalLightData.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::engine {

//! CPU-side local-light shadow candidate collected by LightManager.
/*!
 This snapshot preserves the source `scene::NodeHandle` and the stable
 positional-light index produced for the current frame.

 `node_handle` exists specifically so downstream VSM orchestration can derive a
 deterministic remap identity from the scene-owned light node without inventing
 a parallel identity model or storing handles inside `SceneNodeImpl`.

 `light_index` always indexes the current frame's `PositionalLightData[]`
 snapshot published by `LightManager`.
*/
struct PositionalShadowCandidate {
  scene::NodeHandle node_handle {};
  std::uint32_t light_index { 0U };

  auto operator==(const PositionalShadowCandidate&) const -> bool = default;
};

} // namespace oxygen::engine
