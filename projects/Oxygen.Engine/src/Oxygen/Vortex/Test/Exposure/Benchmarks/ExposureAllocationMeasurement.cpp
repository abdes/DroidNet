//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <numeric>

#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureAllocationScenario.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

auto ExposureAllocationScenario::Snapshot(const std::string& phase)
  -> nlohmann::json
{
  auto record = backend_->MeasureTrackedPlacement();
  record["phase"] = phase;
  record["sequence"] = fixture_.sequence;
  record["slot"] = fixture_.frame.GetFrameSlot().get();
  record["views"] = nlohmann::json::array();
  record["retained_extracts"]
    = std::count_if(retained_.begin(), retained_.end(),
      [](const auto& item) { return item.color.retained_texture != nullptr; });
  record["retained_depth_aliases"] = std::accumulate(retained_.begin(),
    retained_.end(), 0U, [](unsigned count, const auto& item) {
      return count
        + static_cast<unsigned>(std::count_if(item.depths.begin(),
          item.depths.end(),
          [](const auto& depth) { return depth.retained_texture != nullptr; }));
    });
  const auto* owner
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
      *fixture_.renderer_);
  if (owner) {
    const auto [families, leased]
      = vortex::testing::RendererPublicationProbe::SceneTexturePoolCounts(
        *owner);
    record["pool_families"] = families;
    record["leased_families"] = leased;
  }
  WithoutDiagnostics([&] {
    for (const auto& [handle, item] : current_) {
      const auto& exposure = item.color.exposure;
      CHECK_NOTNULL_F(exposure.get());
      const auto state = fixture_.Read<ExposureStateData>(
        *exposure->current_state->buffer, ResourceStates::kShaderResource);
      const auto domain = fixture_.Read<FrameExposureData>(
        *exposure->buffer, ResourceStates::kShaderResource);
      const auto status = fixture_.Read<ExposureStatusStorage>(
        *exposure->current_state->status_buffer, ResourceStates::kCopySource);
      const auto report = fixture_.Read<HdrSuitabilityData>(
        *exposure->suitability_buffer, ResourceStates::kShaderResource);
      record["views"].push_back({ { "handle", handle.get() },
        { "view", item.id.get() }, { "draws", item.draws },
        { "format",
          static_cast<unsigned>(item.color.texture->GetDescriptor().format) },
        { "P", domain.pre_exposure }, { "gain", state.displayed_scale },
        { "lifetime", exposure->current_state->owner_lifetime },
        { "state_words",
          std::bit_cast<std::array<std::uint32_t, sizeof(state) / 4U>>(state) },
        { "frame_words",
          std::bit_cast<std::array<std::uint32_t, sizeof(domain) / 4U>>(
            domain) },
        { "status_words",
          std::bit_cast<std::array<std::uint32_t, sizeof(status) / 4U>>(
            status) },
        { "suitability_words",
          std::bit_cast<std::array<std::uint32_t, sizeof(report) / 4U>>(
            report) } });
    }
  });
  phases_.push_back(record);
  return record;
}

auto ExposureAllocationScenario::Srv(const graphics::Texture& texture)
  -> ShaderVisibleIndex
{
  const auto index
    = fixture_.Backend().GetResourceRegistry().FindShaderVisibleIndex(texture,
      TextureViewDescription { .view_type = ResourceViewType::kTexture_SRV,
        .visibility = DescriptorVisibility::kShaderVisible,
        .format = texture.GetDescriptor().format,
        .dimension = texture.GetDescriptor().texture_type,
        .sub_resources = TextureSubResourceSet::EntireTexture() });
  CHECK_F(index.has_value());
  return *index;
}

auto ExposureAllocationScenario::Population(const nlohmann::json& snapshot)
  -> std::vector<std::string>
{
  std::vector<std::string> rows;
  for (const auto* kind : { "textures", "buffers" }) {
    for (auto row : snapshot.at(kind)) {
      row.erase("id");
      row.erase("registered");
      rows.push_back(std::string(kind) + row.dump());
    }
  }
  std::ranges::sort(rows);
  return rows;
}

auto ExposureAllocationScenario::EnsureDepthReadback(
  unsigned index, const graphics::Texture& depth) -> void
{
  auto& readback = depth_readbacks_[index];
  if (readback.buffer) {
    ASSERT_EQ(readback.footprint.Footprint.Width, depth.GetDescriptor().width);
    ASSERT_EQ(
      readback.footprint.Footprint.Height, depth.GetDescriptor().height);
    return;
  }
  auto* source = depth.GetNativeResource()->AsPointer<ID3D12Resource>();
  const auto desc = source->GetDesc();
  UINT rows = 0U;
  UINT64 bytes = 0U;
  fixture_.Backend().GetCurrentDevice()->GetCopyableFootprints(
    &desc, 0U, 1U, 0U, &readback.footprint, &rows, &readback.row_bytes, &bytes);
  ASSERT_EQ(rows, depth.GetDescriptor().height);
  ASSERT_GT(bytes, 0U);
  ASSERT_NE(bytes, (std::numeric_limits<UINT64>::max)());
  constexpr UINT64 alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
  readback.alias_stride = (bytes + alignment - 1U) & ~(alignment - 1U);
  readback.buffer = fixture_.Backend().CreateBuffer(
    { .size_bytes = 2U * readback.alias_stride,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kReadBack,
      .debug_name
      = "LifecycleAccounting.DepthReadback" + std::to_string(index) });
  ASSERT_NE(readback.buffer, nullptr);
  fixture_.Backend().GetResourceRegistry().Register(readback.buffer);
}

auto ExposureAllocationScenario::CopyDepth(graphics::CommandRecorder& recorder,
  const graphics::Texture& depth, unsigned index, unsigned alias) -> void
{
  const auto& readback = depth_readbacks_[index];
  CHECK_NOTNULL_F(readback.buffer.get());
  if (!recorder.IsResourceTracked(depth)) {
    CHECK_F(recorder.AdoptKnownResourceState(depth));
  }
  if (!recorder.IsResourceTracked(*readback.buffer)
    && !recorder.AdoptKnownResourceState(*readback.buffer)) {
    recorder.BeginTrackingResourceState(
      *readback.buffer, ResourceStates::kCopyDest);
  }
  recorder.RequireResourceState(depth, ResourceStates::kCopySource);
  recorder.RequireResourceState(*readback.buffer, ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  D3D12_TEXTURE_COPY_LOCATION source {};
  source.pResource = depth.GetNativeResource()->AsPointer<ID3D12Resource>();
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  source.SubresourceIndex = 0U;
  D3D12_TEXTURE_COPY_LOCATION destination {};
  destination.pResource
    = readback.buffer->GetNativeResource()->AsPointer<ID3D12Resource>();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint = readback.footprint;
  destination.PlacedFootprint.Offset += alias * readback.alias_stride;
  const auto recording = recorder.GetCommandListForInspection();
  const auto* native
    = static_cast<const graphics::d3d12::CommandList*>(recording.get());
  native->GetCommandList()->CopyTextureRegion(
    &destination, 0U, 0U, 0U, &source, nullptr);
}

auto ExposureAllocationScenario::DepthSample(unsigned index, unsigned alias)
  -> std::uint32_t
{
  const auto& readback = depth_readbacks_[index];
  const auto width = readback.footprint.Footprint.Width;
  CHECK_EQ_F(readback.row_bytes % width, 0U);
  const auto stride = readback.row_bytes / width;
  CHECK_GE_F(stride, sizeof(std::uint32_t));
  const auto* bytes = static_cast<const std::byte*>(readback.buffer->Map());
  CHECK_NOTNULL_F(bytes);
  std::uint32_t bits = 0U;
  std::memcpy(&bits,
    bytes + readback.footprint.Offset + alias * readback.alias_stride
      + (readback.footprint.Footprint.Height / 2U)
        * readback.footprint.Footprint.RowPitch
      + (width / 2U) * stride,
    sizeof(bits));
  readback.buffer->UnMap();
  return bits;
}

} // namespace oxygen::vortex::testing::exposure
