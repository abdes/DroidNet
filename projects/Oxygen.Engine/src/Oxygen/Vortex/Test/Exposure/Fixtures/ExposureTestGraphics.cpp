//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>

#include <unordered_set>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Buffer;
using graphics::BufferDesc;
using graphics::CommandRecorder;
using graphics::Texture;
using graphics::TextureDesc;

auto ExposureFailureGraphics::GetShader(
  const graphics::ShaderRequest& request) const
  -> std::shared_ptr<graphics::IShaderByteCode>
{
  if (request.source_path == "Tests/ToneBoundsProbe.hlsl") {
    return tone_probe;
  }
  return graphics::d3d12::Graphics::GetShader(request);
}

auto ExposureFailureGraphics::IsExposureHdrTexture(std::string_view name)
  -> bool
{
  return name == "SceneColor" || name == "ResolvedSceneColor"
    || name.contains("SkyView") || name.contains("Aerial")
    || name.contains("IntegratedLightScattering")
    || name.contains("AtmosphereTransmittance")
    || name.contains("AtmosphereMultiScattering");
}

auto ExposureFailureGraphics::MeasureTrackedPlacement() const -> nlohmann::json
{
  auto textures = nlohmann::json::array();
  auto buffers = nlohmann::json::array();
  std::unordered_set<ID3D12Resource*> unique;
  std::uint64_t texture_bytes = 0U;
  std::uint64_t buffer_bytes = 0U;
  std::uint64_t hdr_bytes = 0U;
  std::uint64_t fixture_output_bytes = 0U;
  std::uint64_t delayed_output_bytes = 0U;
  std::uint64_t depth_readback_bytes = 0U;
  for (std::size_t ordinal = 0U; ordinal < tracked_textures.size(); ++ordinal) {
    const auto texture = tracked_textures.at(ordinal).lock();
    if (!texture) {
      continue;
    }
    auto* native = texture->GetNativeResource()->AsPointer<ID3D12Resource>();
    if ((native == nullptr) || !unique.insert(native).second) {
      continue;
    }
    const auto shape = native->GetDesc();
    const auto bytes = GetCurrentDevice()
                         ->GetResourceAllocationInfo(0U, 1U, &shape)
                         .SizeInBytes;
    const auto& desc = texture->GetDescriptor();
    const bool hdr = IsExposureHdrTexture(desc.debug_name);
    const bool fixture_output
      = desc.debug_name.starts_with("LifecycleAccounting.Output");
    const bool delayed_output
      = desc.debug_name.starts_with("LifecycleAccounting.DelayedConsumer");
    textures.push_back({
      {
        "id",
        ordinal,
      },
      {
        "name",
        desc.debug_name,
      },
      {
        "format",
        static_cast<unsigned>(desc.format),
      },
      {
        "width",
        desc.width,
      },
      {
        "height",
        desc.height,
      },
      {
        "depth",
        desc.depth,
      },
      {
        "array_layers",
        desc.array_size,
      },
      {
        "mips",
        desc.mip_levels,
      },
      {
        "samples",
        desc.sample_count,
      },
      {
        "sample_quality",
        desc.sample_quality,
      },
      {
        "texture_type",
        static_cast<unsigned>(desc.texture_type),
      },
      {
        "native_format",
        static_cast<unsigned>(shape.Format),
      },
      {
        "native_flags",
        static_cast<unsigned>(shape.Flags),
      },
      {
        "native_layout",
        static_cast<unsigned>(shape.Layout),
      },
      {
        "native_alignment",
        shape.Alignment,
      },
      {
        "native_dimension",
        static_cast<unsigned>(shape.Dimension),
      },
      {
        "placement_bytes",
        bytes,
      },
      {
        "exposure_hdr",
        hdr,
      },
      {
        "fixture_output",
        fixture_output || delayed_output,
      },
      {
        "registered",
        GetResourceRegistry().Contains(*texture),
      },
    });
    texture_bytes += bytes;
    if (hdr) {
      hdr_bytes += bytes;
    }
    if (fixture_output) {
      fixture_output_bytes += bytes;
    }
    if (delayed_output) {
      delayed_output_bytes += bytes;
    }
  }
  for (std::size_t ordinal = 0U; ordinal < tracked_buffers.size(); ++ordinal) {
    const auto buffer = tracked_buffers.at(ordinal).lock();
    if (!buffer) {
      continue;
    }
    auto* native = buffer->GetNativeResource()->AsPointer<ID3D12Resource>();
    if ((native == nullptr) || !unique.insert(native).second) {
      continue;
    }
    const auto shape = native->GetDesc();
    const auto bytes = GetCurrentDevice()
                         ->GetResourceAllocationInfo(0U, 1U, &shape)
                         .SizeInBytes;
    const auto desc = buffer->GetDescriptor();
    const bool depth_readback
      = desc.debug_name.starts_with("LifecycleAccounting.DepthReadback");
    buffers.push_back({
      {
        "id",
        ordinal,
      },
      {
        "name",
        desc.debug_name,
      },
      {
        "logical_bytes",
        desc.size_bytes,
      },
      {
        "placement_bytes",
        bytes,
      },
      {
        "memory",
        static_cast<unsigned>(desc.memory),
      },
      {
        "usage",
        static_cast<unsigned>(desc.usage),
      },
      {
        "native_flags",
        static_cast<unsigned>(shape.Flags),
      },
      {
        "native_layout",
        static_cast<unsigned>(shape.Layout),
      },
      {
        "native_alignment",
        shape.Alignment,
      },
      {
        "fixture_output",
        depth_readback,
      },
      {
        "registered",
        GetResourceRegistry().Contains(*buffer),
      },
    });
    buffer_bytes += bytes;
    if (depth_readback) {
      depth_readback_bytes += bytes;
    }
  }
  return {
    {
      "textures",
      std::move(textures),
    },
    {
      "buffers",
      std::move(buffers),
    },
    {
      "texture_placement_bytes",
      texture_bytes,
    },
    {
      "buffer_placement_bytes",
      buffer_bytes,
    },
    {
      "hdr_placement_bytes",
      hdr_bytes,
    },
    {
      "total_placement_bytes",
      texture_bytes + buffer_bytes,
    },
    {
      "fixture_output_placement_bytes",
      fixture_output_bytes,
    },
    {
      "delayed_output_placement_bytes",
      delayed_output_bytes,
    },
    {
      "depth_readback_placement_bytes",
      depth_readback_bytes,
    },
    {
      "engine_placement_bytes",
      texture_bytes + buffer_bytes - fixture_output_bytes - delayed_output_bytes
        - depth_readback_bytes,
    },
    {
      "texture_creation_count",
      tracked_textures.size(),
    },
    {
      "buffer_creation_count",
      tracked_buffers.size(),
    },
  };
}

auto ExposureFailureGraphics::ObserveAllocationPeak() const -> void
{
  auto snapshot = MeasureTrackedPlacement();
  auto changed = nlohmann::json::array();
  struct PeakField {
    const char* category;
    const char* snapshot_key;
  };
  const auto update = [&](PeakField field, std::uint64_t& peak) -> void {
    const auto bytes = snapshot.at(field.snapshot_key).get<std::uint64_t>();
    if (bytes > peak) {
      peak = bytes;
      allocation_peaks.update({
        {
          field.category,
          {
            {
              "iteration",
              accounting_iteration,
            },
            {
              "phase",
              accounting_phase,
            },
            {
              "bytes",
              bytes,
            },
            {
              "history_index",
              allocation_peak_history.size(),
            },
          },
        },
      });
      changed.push_back(field.category);
    }
  };
  update(
    {
      .category = "textures",
      .snapshot_key = "texture_placement_bytes",
    },
    peak_texture_bytes);
  update(
    {
      .category = "buffers",
      .snapshot_key = "buffer_placement_bytes",
    },
    peak_buffer_bytes);
  update(
    {
      .category = "combined",
      .snapshot_key = "total_placement_bytes",
    },
    peak_placement_bytes);
  update(
    {
      .category = "engine",
      .snapshot_key = "engine_placement_bytes",
    },
    peak_engine_placement_bytes);
  const auto previous_hdr = peak_hdr_bytes;
  update(
    {
      .category = "hdr",
      .snapshot_key = "hdr_placement_bytes",
    },
    peak_hdr_bytes);
  if (peak_hdr_bytes != previous_hdr) {
    peak_hdr_iteration = accounting_iteration;
  }
  if (!changed.empty()) {
    allocation_peak_history.push_back({
      {
        "iteration",
        accounting_iteration,
      },
      {
        "phase",
        accounting_phase,
      },
      {
        "changed_categories",
        std::move(changed),
      },
      {
        "resources",
        std::move(snapshot),
      },
    });
  }
}

auto ExposureFailureGraphics::CreateBuffer(const BufferDesc& desc) const
  -> std::shared_ptr<graphics::Buffer>
{
  auto buffer = graphics::d3d12::Graphics::CreateBuffer(desc);
  if (track_resources && buffer) {
    tracked_buffers.push_back(buffer);
  }
  if (account_texture_allocations && buffer) {
    ObserveAllocationPeak();
  }
  return buffer;
}

auto ExposureFailureGraphics::CreateTexture(const TextureDesc& desc) const
  -> std::shared_ptr<graphics::Texture>
{
  auto texture = graphics::d3d12::Graphics::CreateTexture(desc);
  if (desc.debug_name == "Vortex.StaticSkyLight.ProcessedCubemap") {
    processed_sky = texture;
  }
  if (track_resources && texture) {
    tracked_textures.push_back(texture);
  }
  if (account_texture_allocations && texture) {
    ObserveAllocationPeak();
  }
  return texture;
}

auto ExposureFailureGraphics::AcquireCommandRecorder(
  const graphics::QueueKey& queue, std::string_view name,
  const graphics::SubmissionPolicy policy) -> graphics::CommandRecording
{
  recorder_names.emplace_back(name);
  if (!fail_recorder_name.empty() && name == fail_recorder_name) {
    return {};
  }
  if (fail_status_recorder && name == "Exposure status readback") {
    return {};
  }
  if (fail_next_frame_recorder && name == "Vortex Exposure Frame") {
    fail_next_frame_recorder = false;
    return {};
  }
  if (fail_next_exposure_recorder && name == "Vortex Exposure") {
    fail_next_exposure_recorder = false;
    return {};
  }
  return graphics::d3d12::Graphics::AcquireCommandRecorder(queue, name, policy);
}

} // namespace oxygen::vortex::testing::exposure
