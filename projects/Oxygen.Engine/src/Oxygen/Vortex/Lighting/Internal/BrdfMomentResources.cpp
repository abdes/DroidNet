//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Lighting/Internal/BrdfMomentData.h>
#include <Oxygen/Vortex/Lighting/Internal/BrdfMomentResources.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadPlanner.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>

namespace oxygen::vortex::lighting::internal {

BrdfMomentResources::BrdfMomentResources(Renderer& renderer)
  : renderer_(renderer)
{
}

BrdfMomentResources::~BrdfMomentResources()
{
  if (auto graphics = renderer_.GetGraphics()) {
    auto* registry = &graphics->GetResourceRegistry();
    for (auto& texture : textures_) {
      if (texture) {
        graphics->GetDeferredReclaimer().RegisterDeferredAction(
          [registry, resource = std::move(texture)] mutable -> void {
            if (registry->Contains(*resource)) {
              registry->UnRegisterResource(*resource);
            }
            resource.reset();
          });
      }
    }
  }
}

auto BrdfMomentResources::Prepare()
  -> std::expected<void, LightingPreparationFailure>
{
  if (initialized_) {
    return {};
  }
  const auto data = GetBrdfMomentData();
  if (!data) {
    return std::unexpected(LightingPreparationFailure {
      .error = LightingPreparationError::kMissingBrdfData,
    });
  }
  const auto graphics = renderer_.GetGraphics();
  const auto failure = LightingPreparationFailure {
    .error = LightingPreparationError::kAllocationFailed,
  };
  if (!graphics) {
    return std::unexpected(failure);
  }
  auto& registry = graphics->GetResourceRegistry();
  const auto sources = std::array { data->moments, data->means };
  const auto widths = std::array { data->view_nodes, 1U };
  const auto names = std::array {
    "LightingService.BrdfMoments",
    "LightingService.BrdfMeanMoments",
  };
  auto staging = std::array<std::shared_ptr<graphics::Buffer>, 2> {};
  auto regions = std::array<graphics::TextureUploadRegion, 2> {};
  const auto queue = graphics->QueueKeyFor(graphics::QueueRole::kGraphics);
  const auto policy = upload::UploadPolicy { queue };
  for (std::size_t index = 0; index < textures_.size(); ++index) {
    auto& texture = textures_.at(index);
    if (!texture) {
      texture = graphics->CreateTexture({
        .width = widths.at(index),
        .height = data->roughness_nodes,
        .format = Format::kRG32Float,
        .texture_type = TextureType::kTexture2D,
        .debug_name = names.at(index),
        .is_shader_resource = true,
        .initial_state = graphics::ResourceStates::kCommon,
        .allocation_budget
        = { .owner = renderer_.GetLightingAllocationBudget() },
      });
      if (!texture) {
        return std::unexpected(failure);
      }
      registry.Register(texture);
    }
    if (!slots_.at(index).IsValid()) {
      auto allocation = graphics->GetDescriptorAllocator().AllocateBindless(
        bindless::generated::kTexturesDomain,
        graphics::ResourceViewType::kTexture_SRV);
      if (!allocation.IsValid()) {
        return std::unexpected(failure);
      }
      const auto slot
        = graphics->GetDescriptorAllocator().GetShaderVisibleIndex(allocation);
      registry.RegisterView(*texture, std::move(allocation),
        graphics::TextureViewDescription {
          .view_type = graphics::ResourceViewType::kTexture_SRV,
          .visibility = graphics::DescriptorVisibility::kShaderVisible,
          .format = Format::kRG32Float,
          .dimension = TextureType::kTexture2D,
          .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
        });
      slots_.at(index) = slot;
    }
    const auto plan
      = upload::UploadPlanner::PlanTexture2D({ .dst = texture }, {}, policy);
    if (!plan || plan->regions.size() != 1U) {
      return std::unexpected(failure);
    }
    auto& buffer = staging.at(index);
    buffer = graphics->CreateBuffer({
      .size_bytes = plan->total_bytes,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = names.at(index),
      .allocation_budget = { .owner = renderer_.GetLightingAllocationBudget() },
    });
    if (!buffer) {
      return std::unexpected(failure);
    }
    auto& region = regions.at(index);
    region = plan->regions.front();
    constexpr auto kMomentBytes = sizeof(float) * 2U;
    const auto source_pitch
      = static_cast<std::size_t>(widths.at(index)) * kMomentBytes;
    auto* mapped = static_cast<std::byte*>(buffer->Map());
    if (mapped == nullptr) {
      return std::unexpected(failure);
    }
    auto destination
      = std::span(mapped, static_cast<std::size_t>(plan->total_bytes));
    for (std::uint32_t row = 0; row < data->roughness_nodes; ++row) {
      std::memcpy(
        destination
          .subspan(region.buffer_offset
              + (static_cast<std::size_t>(row) * region.buffer_row_pitch),
            source_pitch)
          .data(),
        sources.at(index)
          .subspan(static_cast<std::size_t>(row) * source_pitch, source_pitch)
          .data(),
        source_pitch);
    }
    buffer->UnMap();
  }
  auto recording = graphics->AcquireCommandRecorder(queue,
    "Vortex.Lighting.InitializeBrdfMoments",
    graphics::SubmissionPolicy::kExplicit);
  if (!recording) {
    return std::unexpected(failure);
  }
  for (std::size_t index = 0; index < textures_.size(); ++index) {
    auto& texture = *textures_.at(index);
    recording->BeginTrackingResourceState(
      *staging.at(index), graphics::ResourceStates::kGenericRead);
    recording->BeginTrackingResourceState(
      texture, graphics::ResourceStates::kCommon);
    recording->RequireResourceState(
      texture, graphics::ResourceStates::kCopyDest);
    recording->FlushBarriers();
    recording->CopyBufferToTexture(
      *staging.at(index), regions.at(index), texture);
    recording->RequireResourceStateFinal(
      texture, graphics::ResourceStates::kShaderResource);
  }
  if (!recording.Submit()) {
    return std::unexpected(failure);
  }
  for (auto& buffer : staging) {
    graphics->GetDeferredReclaimer().RegisterDeferredAction(
      [owner = graphics.get(), resource = std::move(buffer)] mutable -> void {
        owner->ForgetKnownResourceState(resource->GetNativeResource());
        resource.reset();
      });
  }
  initialized_ = true;
  return {};
}

auto BrdfMomentResources::Publish(LightingFrameBindings& bindings) const -> void
{
  if (initialized_) {
    bindings.brdf_moments_srv = slots_.at(0);
    bindings.brdf_mean_moments_srv = slots_.at(1);
    bindings.brdf_model_revision = kBrdfModelRevision;
  }
}

} // namespace oxygen::vortex::lighting::internal
