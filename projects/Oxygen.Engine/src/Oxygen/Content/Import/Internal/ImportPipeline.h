//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <string_view>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

#include <Oxygen/Composition/Typed.h>

namespace oxygen::content::import {

//! Kinds of import plan items.
enum class PlanItemKind : uint8_t {
  kTextureResource,
  kBufferResource,
  kAudioResource,
  kMaterialAsset,
  kMeshBuild,
  kGeometryAsset,
  kSceneAsset,
};

//! Convert a plan item kind to a string view.
[[nodiscard]] inline auto to_string(PlanItemKind kind) noexcept
  -> std::string_view
{
  switch (kind) {
  case PlanItemKind::kTextureResource:
    return "Texture";
  case PlanItemKind::kBufferResource:
    return "Buffer";
  case PlanItemKind::kAudioResource:
    return "Audio";
  case PlanItemKind::kMaterialAsset:
    return "Material";
  case PlanItemKind::kMeshBuild:
    return "MeshBuild";
  case PlanItemKind::kGeometryAsset:
    return "Geometry";
  case PlanItemKind::kSceneAsset:
    return "Scene";
  }

  return "Unknown";
}

inline constexpr size_t kPlanKindCount = 7;

//! Progress counters for a resource pipeline.
/*!
 Tracks submitted and completed work items to enable progress reporting.

 ### Invariants

 - All counters are non-negative and use zero as a valid default state.
 - `submitted` is monotonically non-decreasing and increments when a work item
   is accepted.
 - `completed` is monotonically non-decreasing and increments on successful
   results.
 - `failed` is monotonically non-decreasing and increments on failed results.
 - `in_flight = submitted - completed - failed`.
 - When the pipeline is drained: `in_flight == 0` and
   `submitted == completed + failed`.
*/
struct PipelineProgress {
  size_t submitted = 0;
  size_t completed = 0;
  size_t failed = 0;
  size_t in_flight = 0;
  float throughput = 0.0F;
};

//! Concept defining the required API surface for resource pipelines.
/*!
 All pipelines (texture, audio, mesh) satisfy this concept while using their
 own `WorkItem` and `WorkResult` types.
*/
template <typename T>
concept ImportPipeline = oxygen::IsTyped<T>
  && std::movable<typename T::WorkItem> && std::movable<typename T::WorkResult>
  && requires(T& pipeline, typename T::WorkItem item, co::Nursery& nursery) {
       typename T::WorkItem;
       typename T::WorkResult;
       { T::kItemKind } -> std::convertible_to<PlanItemKind>;

       { pipeline.Start(nursery) } -> std::same_as<void>;
       { pipeline.Submit(std::move(item)) } -> std::same_as<co::Co<>>;
       { pipeline.Collect() } -> std::same_as<co::Co<typename T::WorkResult>>;

       { pipeline.HasPending() } -> std::convertible_to<bool>;
       { pipeline.PendingCount() } -> std::convertible_to<size_t>;
       { pipeline.GetProgress() } -> std::same_as<PipelineProgress>;
       { pipeline.OutputQueueSize() } -> std::convertible_to<size_t>;
       { pipeline.OutputQueueCapacity() } -> std::convertible_to<size_t>;
     };

} // namespace oxygen::content::import
