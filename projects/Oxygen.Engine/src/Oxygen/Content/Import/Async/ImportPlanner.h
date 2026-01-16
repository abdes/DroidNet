//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Content/Import/Async/ImportPipeline.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/OxCo/Event.h>

namespace oxygen::content::import {

//! Kinds of import plan items.
enum class PlanItemKind : uint8_t {
  kTextureResource,
  kBufferResource,
  kAudioResource,
  kMaterialAsset,
  kGeometryAsset,
  kSceneAsset,
};

//! Convert a plan item kind to a string view.
OXGN_CNTT_NDAPI auto to_string(PlanItemKind kind) noexcept -> std::string_view;

inline constexpr size_t kPlanKindCount = 6;

//! Strongly typed identifier for a plan item.
using PlanItemId = oxygen::NamedType<uint32_t, struct PlanItemIdTag,
  // clang-format off
  oxygen::DefaultInitialized,
  oxygen::Hashable,
  oxygen::Comparable,
  oxygen::Printable
  // clang-format on
  >;

//! Strongly typed handle for importer-owned payload references.
using WorkPayloadHandle = oxygen::NamedType<void*, struct WorkPayloadHandleTag,
  // clang-format off
  oxygen::DefaultInitialized,
  oxygen::Hashable,
  oxygen::Comparable,
  oxygen::Printable
  // clang-format on
  >;

//! Token used to mark a dependency as satisfied.
struct DependencyToken {
  PlanItemId producer {};
};

//! Readiness event for a plan item.
struct ReadinessEvent {
  co::Event event;
  bool ready = false;
};

//! Tracks readiness for a consumer item.
struct ReadinessTracker {
  std::span<const PlanItemId> required;
  std::span<uint8_t> satisfied;
  ReadinessEvent* ready_event = nullptr;

  //! Check whether all dependencies are satisfied.
  OXGN_CNTT_NDAPI auto IsReady() const noexcept -> bool;

  //! Mark a producer dependency as ready.
  OXGN_CNTT_API auto MarkReady(const DependencyToken& token) -> bool;
};

//! Declared item in the import plan.
struct PlanItem {
  PlanItemId id {};
  PlanItemKind kind = PlanItemKind::kTextureResource;
  std::string debug_name;
  WorkPayloadHandle work_handle {};
};

//! Execution step derived from a plan item.
struct PlanStep {
  PlanItemId item_id {};
  std::span<const ReadinessEvent* const> prerequisites;
};

//! Planner that owns the dependency graph and readiness tracking.
/*!
 Builds a stable, linear execution plan for import steps and manages readiness
 tracking events used during async import execution.

 ### Key Features

 - **Stable Topological Order**: Deterministic ordering based on registration
   order for tie-breaking.
 - **Readiness Tracking**: Per-item readiness events for dependency gating.
 - **Pipeline Registry**: Injectable pipeline type IDs for tests and mocks.

 @warning ImportPlanner is job-scoped and not thread-safe.
*/
class ImportPlanner final {
public:
  ImportPlanner() = default;
  ~ImportPlanner() = default;

  OXYGEN_MAKE_NON_COPYABLE(ImportPlanner)
  OXYGEN_MAKE_NON_MOVABLE(ImportPlanner)

  //=== High-level plan construction ===------------------------------------//

  //! Register a texture resource plan item.
  OXGN_CNTT_API auto AddTextureResource(
    std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId;

  //! Register a buffer resource plan item.
  OXGN_CNTT_API auto AddBufferResource(
    std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId;

  //! Register an audio resource plan item.
  OXGN_CNTT_API auto AddAudioResource(
    std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId;

  //! Register a material asset plan item.
  OXGN_CNTT_API auto AddMaterialAsset(
    std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId;

  //! Register a geometry asset plan item.
  OXGN_CNTT_API auto AddGeometryAsset(
    std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId;

  //! Register a scene asset plan item.
  OXGN_CNTT_API auto AddSceneAsset(
    std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId;

  //! Add a dependency edge from consumer to producer.
  OXGN_CNTT_API auto AddDependency(PlanItemId consumer, PlanItemId producer)
    -> void;

  //=== Pipeline registration ===--------------------------------------------//

  //! Register a pipeline type for a plan item kind.
  template <ImportPipeline Pipeline>
  auto RegisterPipeline(PlanItemKind kind) -> void
  {
    const auto index = static_cast<size_t>(kind);
    pipeline_registry_.at(index) = Pipeline::ClassTypeId();
  }

  //! Build the execution plan and seal the planner.
  OXGN_CNTT_API auto MakePlan() -> std::vector<PlanStep>;

  //! Access a plan item by ID.
  OXGN_CNTT_API auto Item(PlanItemId item) -> PlanItem&;

  //! Resolve the pipeline type ID for a plan item.
  OXGN_CNTT_NDAPI auto PipelineTypeFor(PlanItemId item) const noexcept
    -> std::optional<oxygen::TypeId>;

  //! Access the readiness tracker for a plan item.
  OXGN_CNTT_API auto Tracker(PlanItemId item) -> ReadinessTracker&;

  //! Access the readiness event for a plan item.
  OXGN_CNTT_API auto ReadyEvent(PlanItemId item) -> ReadinessEvent&;

private:
  auto AddItem(PlanItemKind kind, std::string debug_name,
    WorkPayloadHandle work_handle) -> PlanItemId;

  auto ItemIndex(PlanItemId item) const -> size_t;

  auto EnsureMutable() const -> void;

  bool sealed_ = false;
  std::vector<PlanItem> items_;
  std::vector<std::vector<PlanItemId>> dependencies_;
  std::vector<ReadinessEvent> events_;
  std::vector<ReadinessTracker> trackers_;
  std::vector<PlanItemId> required_storage_;
  std::vector<uint8_t> satisfied_storage_;
  std::vector<const ReadinessEvent*> prerequisites_storage_;
  std::array<std::optional<oxygen::TypeId>, kPlanKindCount>
    pipeline_registry_ {};
};

} // namespace oxygen::content::import
