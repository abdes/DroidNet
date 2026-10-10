//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Internal/MeshRasterState.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::vortex {
class Renderer;
} // namespace oxygen::vortex

namespace oxygen::vortex::occlusion::internal {

//! One candidate draw of a list, in the list's CPU order.
struct IndirectDrawCandidate {
  std::uint32_t draw_index { 0U };
  std::uint32_t vertex_count { 0U };
  std::uint32_t instance_count { 1U };
  vortex::internal::MeshRasterState raster_state {};
};

//! A pass draw command a list can carry.
template <typename Command>
concept IndirectDrawSource = requires(const Command& command) {
  { command.draw_index } -> std::convertible_to<std::uint32_t>;
  { command.instance_count } -> std::convertible_to<std::uint32_t>;
  { command.VertexCountPerInstance() } -> std::convertible_to<std::uint32_t>;
};

//! Makes the candidate for one pass draw command.
/*!
 The counts come from the command, the raster state from the draw's metadata.
*/
template <IndirectDrawSource Command>
[[nodiscard]] auto MakeIndirectDrawCandidate(
  const std::span<const DrawMetadata> metadata, const Command& command)
  -> IndirectDrawCandidate
{
  return IndirectDrawCandidate {
    .draw_index = command.draw_index,
    .vertex_count = command.VertexCountPerInstance(),
    .instance_count = command.instance_count,
    .raster_state
    = vortex::internal::ResolveMeshRasterState(metadata, command.draw_index),
  };
}

//! Makes the candidates for a pass's CPU-ordered draw commands.
template <IndirectDrawSource Command>
[[nodiscard]] auto MakeIndirectDrawCandidates(
  const std::span<const DrawMetadata> metadata,
  const std::span<const Command> commands) -> std::vector<IndirectDrawCandidate>
{
  auto candidates = std::vector<IndirectDrawCandidate> {};
  candidates.reserve(commands.size());
  for (const auto& command : commands) {
    candidates.push_back(MakeIndirectDrawCandidate(metadata, command));
  }
  return candidates;
}

//! A run of consecutive candidates that share one pipeline.
struct IndirectDrawSegment {
  vortex::internal::MeshRasterState raster_state {};
  std::uint32_t first_candidate { 0U };
  std::uint32_t candidate_count { 0U };
};

//! The GPU-compacted commands of one list, valid until the end of the frame.
/*!
 `arguments` holds the commands; a segment's commands start at its first
 candidate, so its capacity is its candidate count. `counts` holds one `uint`
 count per segment.
*/
struct IndirectDrawList {
  observer_ptr<const graphics::Buffer> arguments;
  observer_ptr<const graphics::Buffer> counts;
  std::vector<IndirectDrawSegment> segments;

  [[nodiscard]] auto IsEmpty() const noexcept -> bool
  {
    return arguments == nullptr || counts == nullptr || segments.empty();
  }
};

//! One indirect draw command: a D3D12_DRAW_ARGUMENTS record.
/*!
 Lists use the plain DRAW command signature. The draw index travels in
 `start_instance_location`, which mesh shaders read as
 `SV_StartInstanceLocation`; no command changes root arguments.
*/
struct IndirectDrawCommand {
  std::uint32_t vertex_count_per_instance { 0U };
  std::uint32_t instance_count { 0U };
  std::uint32_t start_vertex_location { 0U };
  std::uint32_t start_instance_location { 0U };
};
// The stride of the DRAW command signature: sizeof(D3D12_DRAW_ARGUMENTS).
static_assert(sizeof(IndirectDrawCommand) == 16U); // NOLINT(*-magic-numbers)

//! Builds GPU-compacted indirect draw lists from CPU-ordered candidates.
/*!
 The CPU keeps each pass's sort. `Build` cuts the candidates into segments
 wherever the raster state changes, uploads them, and compacts them on the GPU
 against a view's visibility bits, preserving candidate order. `Draw` binds
 each segment's pipeline through the caller and issues one `ExecuteIndirect`
 with the GPU count.

 Each `Build` in a frame gets its own buffers, so lists stay valid until the
 end of the frame. Buffers are reused in later frames.
*/
class IndirectListBuilder {
public:
  //! Binds the pipeline and root arguments for one segment's raster state.
  using BindPipeline
    = std::function<void(const vortex::internal::MeshRasterState&)>;

  OXGN_VRTX_API IndirectListBuilder(
    Renderer& renderer, std::string_view debug_name);
  OXGN_VRTX_API ~IndirectListBuilder();

  OXYGEN_MAKE_NON_COPYABLE(IndirectListBuilder)
  OXYGEN_MAKE_NON_MOVABLE(IndirectListBuilder)

  //! Records the compaction of `candidates`; an empty list when it cannot.
  /*!
   Must be recorded outside a render pass: it binds compute pipelines. A
   candidate is kept when the predicate keeps its draw's visibility bits;
   without valid `visibility` every candidate is kept.
  */
  OXGN_VRTX_API auto Build(graphics::CommandRecorder& recorder,
    frame::SequenceNumber frame_sequence, frame::Slot frame_slot,
    std::span<const IndirectDrawCandidate> candidates,
    const DrawVisibilityProducts& visibility, DrawVisibilityPredicate predicate)
    -> IndirectDrawList;

  //! Issues every segment of `list`, binding each segment's pipeline first.
  OXGN_VRTX_API static auto Draw(graphics::CommandRecorder& recorder,
    const IndirectDrawList& list, const BindPipeline& bind_pipeline) -> void;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex::occlusion::internal
