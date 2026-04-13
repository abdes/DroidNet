//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <Oxygen/Vortex/ScenePrep/Concepts.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemData.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepState.h>
#include <Oxygen/Vortex/ScenePrep/Types.h>

namespace oxygen::vortex::sceneprep {

auto GeometryUploadFinalizer(const ScenePrepState& state) -> void;
auto TransformUploadFinalizer(const ScenePrepState& state) -> void;
auto MaterialUploadFinalizer(const ScenePrepState& state) -> void;
auto DrawMetadataEmitFinalizer(
  const ScenePrepState& state, const RenderItemData& item) -> void;
auto DrawMetadataSortAndPartitionFinalizer(const ScenePrepState& state) -> void;
auto DrawMetadataUploadFinalizer(const ScenePrepState& state) -> void;

//! Configuration for the Finalization phase (draw preparation).
template < // clang-format off
  typename DrawMetadataEmitFT = void,
  typename DrawMetadataSortFT = void,
  typename GeometryUploadFT = void,
  typename TransformUploadFT = void,
  typename MaterialUploadFT = void,
  typename DrawMetadataUploadFT = void
> // clang-format on
struct FinalizationConfig {
  struct DummyStage_ {
    template <typename... Args>
    constexpr void operator()(Args&&...) const noexcept
    {
    }
  };
  template <typename T>
  using StageOrDummy = std::conditional_t<std::is_void_v<T>, DummyStage_, T>;

  // Essential stages (use `void` to omit)
  [[no_unique_address]] StageOrDummy<DrawMetadataEmitFT> draw_md_emit {};
  [[no_unique_address]] StageOrDummy<DrawMetadataSortFT> draw_md_sort {};
  [[no_unique_address]] StageOrDummy<GeometryUploadFT> geometry_upload {};
  [[no_unique_address]] StageOrDummy<TransformUploadFT> transform_upload {};
  [[no_unique_address]] StageOrDummy<MaterialUploadFT> material_upload {};
  [[no_unique_address]] StageOrDummy<DrawMetadataUploadFT> draw_md_upload {};

  // Presence checks for `if constexpr`
  // clang-format off
  static constexpr bool has_draw_md_emit = !std::is_void_v<DrawMetadataEmitFT>;
  static constexpr bool has_draw_md_sorter = !std::is_void_v<DrawMetadataSortFT>;
  static constexpr bool has_geometry_upload = !std::is_void_v<GeometryUploadFT>;
  static constexpr bool has_transform_upload = !std::is_void_v<TransformUploadFT>;
  static constexpr bool has_material_upload = !std::is_void_v<MaterialUploadFT>;
  static constexpr bool has_draw_md_upload = !std::is_void_v<DrawMetadataUploadFT>;
  // clang-format on
};

//! Provides a complete finalization pipeline that includes all the stages
//! needed by the current Renderer implementation.
inline auto CreateStandardFinalizationConfig()
  -> FinalizationConfig< // clang-format off
    decltype(&DrawMetadataEmitFinalizer),
    decltype(&DrawMetadataSortAndPartitionFinalizer),
    decltype(&GeometryUploadFinalizer),
    decltype(&TransformUploadFinalizer),
    decltype(&MaterialUploadFinalizer),
    decltype(&DrawMetadataUploadFinalizer)
  > // clang-format on
{
  // Concept checks (callables must qualify as finalization stages)
  static_assert(DrawMetadataEmitter<decltype(DrawMetadataEmitFinalizer)>);
  static_assert(Finalizer<decltype(DrawMetadataSortAndPartitionFinalizer)>);
  static_assert(Uploader<decltype(TransformUploadFinalizer)>);
  static_assert(Uploader<decltype(MaterialUploadFinalizer)>);
  static_assert(Uploader<decltype(GeometryUploadFinalizer)>);
  static_assert(Uploader<decltype(DrawMetadataUploadFinalizer)>);

  return {
    .draw_md_emit = &DrawMetadataEmitFinalizer,
    .draw_md_sort = &DrawMetadataSortAndPartitionFinalizer,
    .geometry_upload = &GeometryUploadFinalizer,
    .transform_upload = &TransformUploadFinalizer,
    .material_upload = &MaterialUploadFinalizer,
    .draw_md_upload = &DrawMetadataUploadFinalizer,
  };
}

} // namespace oxygen::vortex::sceneprep
