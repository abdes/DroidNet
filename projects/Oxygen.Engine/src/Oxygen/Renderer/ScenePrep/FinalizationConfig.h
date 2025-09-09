//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <Oxygen/Renderer/ScenePrep/Concepts.h>
#include <Oxygen/Renderer/ScenePrep/Finalizers.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>

namespace oxygen::engine::sceneprep {

//! Configuration for the Finalization phase (draw preparation).
/*!
 Configures essential finalization stages based on actual Renderer needs.
 Following MINIMAL, LEAN AND MEAN philosophy - only includes stages that are
 actually used by the current implementation.

 Essential stages:
 - geometry_uploader: EnsureFrameResources() call
 - transform_uploader: EnsureFrameResources() call
 - material_uploader: EnsureFrameResources() call
 - draw_metadata_emitter: Generate DrawMetadata records (per-item processing)
 - sorter: Sort items and create partitions for efficient rendering (batch
   processing)
 - draw_metadata_uploader: Upload final DrawMetadata to GPU

 All stages are validated using appropriate concepts to ensure type safety and
 correct usage patterns.

 Presence flags (`has_*`) allow compile-time gating with `if constexpr`.
 */
template <typename GeometryUploaderT = void, typename TransformUploaderT = void,
  typename MaterialUploaderT = void, typename DrawMetadataEmitterT = void,
  typename GeometrySrvResolverT = void, typename SorterT = void,
  typename DrawMetadataUploaderT = void>
struct FinalizationConfig {
  struct _DummyStage {
    template <typename... Args>
    constexpr void operator()(Args&&...) const noexcept
    {
    }
  };
  template <typename T>
  using StageOrDummy = std::conditional_t<std::is_void_v<T>, _DummyStage, T>;

  // Essential stages (use `void` to omit)
  [[no_unique_address]] StageOrDummy<GeometryUploaderT> geometry_uploader {};
  [[no_unique_address]] StageOrDummy<TransformUploaderT> transform_uploader {};
  [[no_unique_address]] StageOrDummy<MaterialUploaderT> material_uploader {};
  [[no_unique_address]] StageOrDummy<DrawMetadataEmitterT>
    draw_metadata_emitter {};
  [[no_unique_address]] StageOrDummy<GeometrySrvResolverT>
    geometry_srv_resolver {};
  [[no_unique_address]] StageOrDummy<SorterT> sorter {};
  [[no_unique_address]] StageOrDummy<DrawMetadataUploaderT>
    draw_metadata_uploader {};

  // Presence checks for `if constexpr`
  static constexpr bool has_geometry_uploader
    = !std::is_void_v<GeometryUploaderT>;
  static constexpr bool has_transform_uploader
    = !std::is_void_v<TransformUploaderT>;
  static constexpr bool has_material_uploader
    = !std::is_void_v<MaterialUploaderT>;
  static constexpr bool has_draw_metadata_emitter
    = !std::is_void_v<DrawMetadataEmitterT>;
  static constexpr bool has_geometry_srv_resolver
    = !std::is_void_v<GeometrySrvResolverT>;
  static constexpr bool has_sorter = !std::is_void_v<SorterT>;
  static constexpr bool has_draw_metadata_uploader
    = !std::is_void_v<DrawMetadataUploaderT>;
};

//! Factory: basic finalization configuration with all essential stages.
/*!
 Provides a complete finalization pipeline that includes all the stages
 needed by the current Renderer implementation. Follows the same pattern
 as CreateBasicCollectionConfig.

 All stages satisfy their respective concepts.
 */
inline auto CreateBasicFinalizationConfig()
  -> FinalizationConfig<decltype(&GeometryEnsureFrameResources),
    decltype(&TransformEnsureFrameResources),
    decltype(&MaterialEnsureFrameResources), decltype(&DrawMetadataEmit),
    decltype(&ResolveGeometrySrvIndices), decltype(&SortAndPartition),
    decltype(&DrawMetadataUpload)>
{
  // Concept checks (callables must qualify as finalization stages)
  static_assert(FinalizationUploader<decltype(GeometryEnsureFrameResources)>);
  static_assert(FinalizationUploader<decltype(TransformEnsureFrameResources)>);
  static_assert(FinalizationUploader<decltype(MaterialEnsureFrameResources)>);
  static_assert(DrawMetadataEmitter<decltype(DrawMetadataEmit)>);
  static_assert(FinalizationSorter<decltype(ResolveGeometrySrvIndices)>);
  static_assert(FinalizationSorter<decltype(SortAndPartition)>);
  static_assert(FinalizationUploader<decltype(DrawMetadataUpload)>);

  return {
    .geometry_uploader = &GeometryEnsureFrameResources,
    .transform_uploader = &TransformEnsureFrameResources,
    .material_uploader = &MaterialEnsureFrameResources,
    .draw_metadata_emitter = &DrawMetadataEmit,
    .geometry_srv_resolver = &ResolveGeometrySrvIndices,
    .sorter = &SortAndPartition,
    .draw_metadata_uploader = &DrawMetadataUpload,
  };
}

//! Factory: minimal finalization configuration with only draw metadata emitter.
/*!
 Provides a minimal finalization pipeline with just the draw metadata emitter,
 useful for testing or simplified rendering scenarios.

 @param draw_metadata_emitter Reference to DrawMetadataEmitter instance
 @return Configured FinalizationConfig with only draw metadata emission
 */
template <DrawMetadataEmitter DrawMetadataEmitterT>
inline auto CreateMinimalFinalizationConfig(
  DrawMetadataEmitterT& draw_metadata_emitter)
  -> FinalizationConfig<void, void, void, DrawMetadataEmitterT&>
{
  return {
    .draw_metadata_emitter = draw_metadata_emitter,
  };
}

} // namespace oxygen::engine::sceneprep
