//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <Oxygen/Renderer/ScenePrep/Concepts.h>
#include <Oxygen/Renderer/ScenePrep/Extractors.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemProto.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>

namespace oxygen::engine::sceneprep {

//! Configuration for the Collection phase (scene traversal/extraction).
/*!
 Configures optional per-item extractors used during collection. Each stage
 is a callable with signature compatible with
 `void(const ScenePrepContext&, ScenePrepState&, RenderItemProto&)`.

 Stages:
 - pre_filter: early drop + seeding (visibility, transform, geometry)
 - mesh_resolver: select LOD and resolve mesh
 - visibility_filter: compute `VisibleSubmeshes` (per-submesh frustum culling)
 - producer: emit `RenderItemData` entries from proto state

 Presence flags (`has_*`) allow compile-time gating with `if constexpr`.

 Stage call contract:
 - CPU-only, no GPU calls
 - May mutate `RenderItemProto` and `ScenePrepState`
 - May mark proto as dropped to skip downstream stages
 */
template <typename PreFilterT = void, typename MeshResolverT = void,
  typename VisibilityFilterT = void, typename ProducerT = void>
struct CollectionConfig {
  // Optional stages (use `void` to omit)
  [[no_unique_address]] PreFilterT pre_filter {};
  [[no_unique_address]] MeshResolverT mesh_resolver {};
  [[no_unique_address]] VisibilityFilterT visibility_filter {};
  [[no_unique_address]] ProducerT producer {};

  // Presence checks for `if constexpr`
  static constexpr bool has_pre_filter = !std::is_void_v<PreFilterT>;
  static constexpr bool has_mesh_resolver = !std::is_void_v<MeshResolverT>;
  static constexpr bool has_visibility_filter
    = !std::is_void_v<VisibilityFilterT>;
  static constexpr bool has_producer = !std::is_void_v<ProducerT>;
};

//! Factory: default collection configuration using built-in extractors.
/*!
 Wires the following stages:
 - ExtractionPreFilter
 - MeshResolver
 - SubMeshVisibilityFilter
 - EmitPerVisibleSubmesh

 All stages satisfy `RenderItemDataExtractor`.
 */
inline auto CreateBasicCollectionConfig()
  -> CollectionConfig<decltype(&ExtractionPreFilter), decltype(&MeshResolver),
    decltype(&SubMeshVisibilityFilter), decltype(&EmitPerVisibleSubmesh)>
{
  // Concept checks (callables must qualify as collection extractors)
  static_assert(RenderItemDataExtractor<decltype(ExtractionPreFilter)>);
  static_assert(RenderItemDataExtractor<decltype(MeshResolver)>);
  static_assert(RenderItemDataExtractor<decltype(SubMeshVisibilityFilter)>);
  static_assert(RenderItemDataExtractor<decltype(EmitPerVisibleSubmesh)>);

  return {
    .pre_filter = &ExtractionPreFilter,
    .mesh_resolver = &MeshResolver,
    .visibility_filter = &SubMeshVisibilityFilter,
    .producer = &EmitPerVisibleSubmesh,
  };
}

} // namespace oxygen::engine::sceneprep
