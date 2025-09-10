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
 ### Contracts

 - CPU-only, no GPU calls
 - May mutate `RenderItemProto` and `ScenePrepState`
 - May mark proto as dropped to skip downstream stages
*/
template < // clang-format off
  typename PreFilterT = void,
  typename TransformResolveT = void,
  typename MeshResolverT = void,
  typename VisibilityFilterT = void,
  typename ProducerT = void
> // clang-format on
struct CollectionConfig {
  struct _DummyStage {
    template <typename... Args>
    constexpr void operator()(Args&&...) const noexcept
    {
    }
  };
  template <typename T>
  using StageOrDummy = std::conditional_t<std::is_void_v<T>, _DummyStage, T>;

  // Optional stages (use `void` to omit) – wrapped so that `void` parameters
  // do not declare invalid members yet presence booleans still reflect intent.
  [[no_unique_address]] StageOrDummy<PreFilterT> pre_filter {};
  [[no_unique_address]] StageOrDummy<TransformResolveT> transform_resolve {};
  [[no_unique_address]] StageOrDummy<MeshResolverT> mesh_resolver {};
  [[no_unique_address]] StageOrDummy<VisibilityFilterT> visibility_filter {};
  [[no_unique_address]] StageOrDummy<ProducerT> producer {};

  // Presence checks for `if constexpr`
  // clang-format off
  static constexpr bool has_pre_filter = !std::is_void_v<PreFilterT>;
  static constexpr bool has_transform_resolve = !std::is_void_v<TransformResolveT>;
  static constexpr bool has_mesh_resolver = !std::is_void_v<MeshResolverT>;
  static constexpr bool has_visibility_filter = !std::is_void_v<VisibilityFilterT>;
  static constexpr bool has_producer = !std::is_void_v<ProducerT>;
  // clang-format on
};

//! Provides a complete collection configuration using built-in extractors.
inline auto CreateBasicCollectionConfig()
  -> CollectionConfig< // clang-format off
    decltype(&ExtractionPreFilter),
    decltype(&TransformResolveStage),
    decltype(&MeshResolver),
    decltype(&SubMeshVisibilityFilter),
    decltype(&EmitPerVisibleSubmesh)
  > // clang-format on
{
  // Concept checks (callables must qualify as collection extractors)
  static_assert(RenderItemDataExtractor<decltype(ExtractionPreFilter)>);
  static_assert(RenderItemDataExtractor<decltype(TransformResolveStage)>);
  static_assert(RenderItemDataExtractor<decltype(MeshResolver)>);
  static_assert(RenderItemDataExtractor<decltype(SubMeshVisibilityFilter)>);
  static_assert(RenderItemDataExtractor<decltype(EmitPerVisibleSubmesh)>);

  return {
    .pre_filter = &ExtractionPreFilter,
    .transform_resolve = &TransformResolveStage,
    .mesh_resolver = &MeshResolver,
    .visibility_filter = &SubMeshVisibilityFilter,
    .producer = &EmitPerVisibleSubmesh,
  };
}

} // namespace oxygen::engine::sceneprep
