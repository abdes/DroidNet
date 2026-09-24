//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Benchmarks/ManyLightBaseline.h>
#include <Oxygen/Vortex/Lighting/Types/ClusterLightRange.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Support/D3D12MemoryCapture.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>

namespace oxygen::vortex::testing {

auto ManyLightBaseline::SnapshotResources() -> nlohmann::json
{
  auto memory = D3D12MemoryCapture(MemorySampleCapacity { 1U });
  CHECK_F(memory.Record(MemorySampleId { 0U },
    frame::SequenceNumber { sequence }, Backend().GetMemoryStatistics()));
  const auto budget = renderer_->GetLightingAllocationBudget()->Snapshot();
  const auto counts = FailureBackend().resource_creations.Snapshot();
  CHECK_F(counts.has_value());
  const auto& lighting_staging
    = renderer_->GetLightingStagingProvider().GetStats();
  const auto& shared_staging = renderer_->GetStagingProvider().GetStats();
  const auto* scene_renderer
    = RendererPublicationProbe::GetSceneRenderer(*renderer_);
  const auto* shadows
    = RendererPublicationProbe::GetShadowService(*scene_renderer);
  const auto sharing = shadows->InspectLocalSharing();
  std::uint64_t shadow_bytes = 0, pending_bytes = 0, spare_bytes = 0;
  for (const auto& backing : sharing.backings) {
    const auto desc = backing.texture->GetNativeResource()
                        ->AsPointer<ID3D12Resource>()
                        ->GetDesc();
    const auto bytes = Backend()
                         .GetCurrentDevice()
                         ->GetResourceAllocationInfo(0, 1, &desc)
                         .SizeInBytes;
    shadow_bytes += bytes;
    pending_bytes += backing.closing ? bytes : 0;
    spare_bytes += backing.closing ? 0 : backing.spare_texel_bytes;
  }
  const auto queue = Backend()
                       .GetCommandQueue(graphics::QueueRole::kGraphics)
                       ->InspectSubmissionCounters();
  return { { "allocator", memory.Report() },
    { "local_sharing",
      { { "native_bytes", shadow_bytes },
        { "pending_retirement_bytes", pending_bytes },
        { "spare_texel_bytes", spare_bytes }, { "aliases", sharing.aliases },
        { "live_versions", sharing.live_versions },
        { "canonical_records", sharing.canonical_records },
        { "canonical_record_bytes", sharing.canonical_record_bytes },
        { "version_payload_bytes", sharing.version_payload_bytes },
        { "prepared_request_bytes", sharing.prepared_request_bytes },
        { "cache_hits", sharing.cache_hits },
        { "cache_misses", sharing.cache_misses },
        { "in_place_updates", sharing.in_place_updates },
        { "first_allocations", sharing.first_allocations },
        { "copy_on_write_shape", sharing.copy_on_write_shape },
        { "copy_on_write_family", sharing.copy_on_write_family },
        { "copy_on_write_reader", sharing.copy_on_write_reader } } },
    { "graphics_queue",
      { { "accepted_batches", queue.accepted_batches },
        { "command_lists", queue.command_lists },
        { "completion_signals", queue.completion_signals },
        { "dependency_waits", queue.dependency_waits } } },
    { "resources", FailureBackend().MeasureTrackedPlacement() },
    { "lighting_budget",
      { { "allocated_bytes", budget.allocated.get() },
        { "peak_bytes", budget.peak_allocated.get() },
        { "compact_index_bytes", budget.compact_indices.get() },
        { "rejected_requests", budget.rejected_requests } } },
    { "created_buffers", counts->buffers },
    { "created_textures", counts->textures },
    { "created_buffer_capacity_bytes", counts->requested_buffer_bytes.get() },
    { "lighting_staging_bytes", lighting_staging.total_bytes_allocated },
    { "lighting_staging_allocations", lighting_staging.total_allocations },
    { "shared_staging_bytes", shared_staging.total_bytes_allocated },
    { "shared_staging_allocations", shared_staging.total_allocations } };
}

auto ManyLightBaseline::Capture(const unsigned motion_frame) -> void
{
  const auto capture
    = motion_frame == 0U && request_.value("capture_warmup_frames", 0U) == 0U
    ? BeginOptionalCapture()
    : observer_ptr<graphics::FrameCaptureController> {};
  capture_ = true;
  (void)RenderFrame(motion_frame);
  WaitForQueueIdle();
  capture_ = false;
  if (capture) {
    ASSERT_TRUE(capture->EndCapture());
  }
  for (std::size_t index = 0; index < captured_.size(); ++index) {
    const auto& captured = captured_.at(index);
    const auto& recipe = workload_.views.at(index);
    ASSERT_NE(captured.hdr, nullptr);
    ASSERT_NE(captured.exposure, nullptr);
    ASSERT_EQ(captured.hdr->GetDescriptor().width, recipe.width);
    ASSERT_EQ(captured.hdr->GetDescriptor().height, recipe.height);
    ASSERT_EQ(captured.bindings.local_count, options_.light_count);
    const auto domain = Read<FrameExposureData>(
      *captured.exposure->buffer, graphics::ResourceStates::kShaderResource);
    ASSERT_EQ(domain.pre_exposure, 1.0F);
    const auto pixels = ReadFloatTexture(*captured.hdr, true);
    ASSERT_EQ(
      pixels.size(), static_cast<std::size_t>(recipe.width) * recipe.height);
    std::uint64_t lit_pixels = 0;
    double sum = 0;
    float maximum = 0;
    for (const auto& pixel : pixels) {
      for (unsigned channel = 0; channel < 3U; ++channel) {
        ASSERT_TRUE(std::isfinite(pixel.at(channel)));
        ASSERT_GE(pixel.at(channel), 0.0F);
        maximum = std::max(maximum, pixel.at(channel));
      }
      sum += pixel.at(0);
      lit_pixels += pixel.at(0) > 1.0e-5F ? 1U : 0U;
    }
    if (options_.light_count == 0U) {
      ASSERT_EQ(lit_pixels, 0U);
    } else {
      ASSERT_GT(lit_pixels, 0U);
    }
    const auto filename = "phase-" + std::to_string(motion_frame) + "-view-"
      + std::to_string(index) + ".rgba32f";
    auto output = std::ofstream(directory_ / filename, std::ios::binary);
    ASSERT_TRUE(output.good());
    // Preserve the native float pixels for matched image comparisons.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char*>(pixels.data()),
      static_cast<std::streamsize>(pixels.size() * sizeof(exposure::Pixel)));
    output.close();
    ASSERT_TRUE(output.good());
    auto grid = nlohmann::json::object();
    if (captured.grid.status != nullptr) {
      const auto status = Read<LightGridBuildStatus>(
        *captured.grid.status, graphics::ResourceStates::kShaderResource);
      ASSERT_EQ(status.state, kLightGridBuildValid);
      ASSERT_EQ(status.reason,
        reference_ && options_.light_count > 0U ? kLightGridReasonCapacity
                                                : kLightGridReasonNone);
      ASSERT_EQ(status.required_index_count.at(1), 0U);
      grid = { { "state", status.state }, { "reason", status.reason },
        { "required_indices", status.required_index_count.at(0) },
        { "written_indices", status.written_index_count },
        { "fallback_cells", status.fallback_cell_count },
        { "index_capacity", captured.bindings.index_capacity } };
      if (reference_ && options_.light_count > 0U) {
        ASSERT_GT(status.fallback_cell_count, 0U);
      }
    }
    if (captured.grid.ranges != nullptr) {
      const auto bytes
        = GetReadbackManager()->ReadBufferNow(*captured.grid.ranges,
          { 0U,
            static_cast<std::uint64_t>(captured.bindings.cluster_count)
              * sizeof(ClusterLightRange) });
      ASSERT_TRUE(bytes.has_value());
      auto occupancy
        = std::vector<std::uint32_t>(captured.bindings.cluster_count);
      std::uint64_t references = 0U;
      for (std::size_t cell = 0; cell < occupancy.size(); ++cell) {
        auto range = ClusterLightRange {};
        std::memcpy(
          &range, bytes->data() + cell * sizeof(range), sizeof(range));
        // Complete-list ranges use the invalid offset and the full count.
        occupancy.at(cell) = range.count;
        references += range.count;
      }
      std::ranges::sort(occupancy);
      if (!occupancy.empty()) {
        grid["cell_occupancy"]
          = { { "p50", occupancy.at(occupancy.size() / 2U) },
              { "p95", occupancy.at((occupancy.size() - 1U) * 95U / 100U) },
              { "max", occupancy.back() }, { "cells", occupancy.size() },
              { "candidate_references", references } };
      }
    }
    const auto wide = options_.spot_outer_half_angle_radians
      >= std::numbers::pi_v<float> / 2.0F;
    if (index == 0U) {
      ASSERT_EQ(captured.point_shadows,
        options_.point_shadow_requests
          + (wide ? options_.spot_shadow_requests : 0U));
      ASSERT_EQ(
        captured.spot_shadows, wide ? 0U : options_.spot_shadow_requests);
      ASSERT_EQ(captured.quality_omissions, 0U);
    }
    const auto& sampled
      = options_.moving ? motion_cycle_.at(motion_frame % 240U) : workload_;
    const auto contributors
      = CountGuaranteedVisibleContributors(sampled, recipe);
    if (index == 0U && options_.light_count == 1024U
      && options_.distribution == WorkloadDistribution::kSparse) {
      ASSERT_GE(contributors, 256U);
    }
    images_.push_back({ { "file", filename }, { "phase", motion_frame },
      { "view", index }, { "width", recipe.width }, { "height", recipe.height },
      { "pre_exposure", domain.pre_exposure }, { "lit_pixels", lit_pixels },
      { "mean_red", sum / pixels.size() }, { "maximum_rgb", maximum },
      { "guaranteed_visible_unshadowed_contributors", contributors },
      { "local_count", captured.bindings.local_count },
      { "draws", captured.draws },
      { "point_shadow_maps", captured.point_shadows },
      { "spot_shadow_maps", captured.spot_shadows },
      { "grid", std::move(grid) } });
  }
}

auto ManyLightBaseline::WriteResults() -> void
{
  auto result
    = nlohmann::json { { "schema_version", 1U }, { "request", request_ },
        { "complete", !HasFailure() }, { "images", images_ },
        { "memory", snapshots_ }, { "warmup_frames", warmup_frames_ + 12U },
        { "sample_frames", samples_.size() },
        { "scope",
          "Native offscreen production renderer; no presentation FPS claim. "
          "Frame start includes GPU waits. CPU spans are elapsed wall time. "
          "Readbacks and resource inventories are outside timed frames." } };
  const auto adapter = Backend().GetCurrentDevice()->GetAdapterLuid();
  result["adapter_luid"] = { adapter.LowPart, adapter.HighPart };
  if (measure_) {
    auto input = std::ifstream(directory_ / "gpu.json");
    ASSERT_TRUE(input.good());
    const auto gpu = nlohmann::json::parse(input);
    ASSERT_EQ(gpu.at("complete"), true);
    ASSERT_EQ(gpu.at("timing_valid"), true);
    ASSERT_EQ(gpu.at("written_frames"), sample_frames_);
    result["gpu_complete"] = true;
    result["gpu_timing_valid"] = true;
    result["first_frame_sequence"] = samples_.front().sequence;
    result["last_frame_sequence"] = samples_.back().sequence;
    result["cpu"] = cpu_->Save(directory_ / "cpu-scopes.csv");
    auto csv = std::ofstream(directory_ / "frames.csv");
    csv << std::setprecision(17)
        << "frame_seq,wall_ms,frame_start_ms,scene_update_ms,submission_ms,"
           "shadow_writer_maps,shadow_map_uses,shadow_backing_uses\n";
    for (const auto& sample : samples_) {
      csv << sample.sequence << ',' << sample.wall_ms << ','
          << sample.frame_start_ms << ',' << sample.scene_update_ms << ','
          << sample.submission_ms << ',' << sample.shadow_writers << ','
          << sample.shadow_map_uses << ',' << sample.shadow_backing_uses
          << '\n';
    }
    csv.close();
    ASSERT_TRUE(csv.good());
  }
  auto manifest = std::ofstream(directory_ / "manifest.json");
  manifest << result.dump(2) << '\n';
  manifest.close();
  ASSERT_TRUE(manifest.good());
  RecordProperty("baseline_manifest", (directory_ / "manifest.json").string());
}
} // namespace oxygen::vortex::testing
