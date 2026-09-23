//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <utility>

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
#include <Oxygen/Vortex/Lighting/Internal/BrdfEnergyData.h>
#include <Oxygen/Vortex/Lighting/Internal/BrdfEnergyResources.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadPlanner.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>

namespace oxygen::vortex::lighting::internal {

BrdfEnergyResources::BrdfEnergyResources(Renderer& renderer)
  : renderer_(renderer)
{
}

BrdfEnergyResources::~BrdfEnergyResources()
{
  if (auto graphics = renderer_.GetGraphics(); graphics && texture_) {
    auto* registry = &graphics->GetResourceRegistry();
    graphics->GetDeferredReclaimer().RegisterDeferredAction(
      [registry, resource = std::move(texture_)] mutable -> void {
        if (registry->Contains(*resource)) {
          registry->UnRegisterResource(*resource);
        }
        resource.reset();
      });
  }
}

auto BrdfEnergyResources::Prepare()
  -> std::expected<void, LightingPreparationFailure>
{
  if (initialized_) return {};
  const auto data = GetBrdfEnergyData();
  if (!data) {
    return std::unexpected(LightingPreparationFailure {
      .error = LightingPreparationError::kMissingBrdfData,
    });
  }
  const auto graphics = renderer_.GetGraphics();
  const auto failure = LightingPreparationFailure {
    .error = LightingPreparationError::kAllocationFailed,
  };
  if (!graphics) return std::unexpected(failure);
  auto& registry = graphics->GetResourceRegistry();
  constexpr auto kName = "LightingService.BrdfEnergy";
  if (!texture_) {
    texture_ = graphics->CreateTexture({
      .width = data->view_nodes,
      .height = data->roughness_nodes,
      .format = Format::kRG32Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = kName,
      .is_shader_resource = true,
      .initial_state = graphics::ResourceStates::kCommon,
      .allocation_budget = { .owner = renderer_.GetLightingAllocationBudget() },
    });
    if (!texture_) return std::unexpected(failure);
    registry.Register(texture_);
  }
  if (!slot_.IsValid()) {
    auto allocation = graphics->GetDescriptorAllocator().AllocateBindless(
      bindless::generated::kTexturesDomain, graphics::ResourceViewType::kTexture_SRV);
    if (!allocation.IsValid()) return std::unexpected(failure);
    const auto slot = graphics->GetDescriptorAllocator().GetShaderVisibleIndex(allocation);
    registry.RegisterView(*texture_, std::move(allocation),
      graphics::TextureViewDescription {
        .view_type = graphics::ResourceViewType::kTexture_SRV,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .format = Format::kRG32Float,
        .dimension = TextureType::kTexture2D,
        .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      });
    slot_ = slot;
  }
  const auto queue = graphics->QueueKeyFor(graphics::QueueRole::kGraphics);
  const auto plan = upload::UploadPlanner::PlanTexture2D(
    { .dst = texture_ }, {}, upload::UploadPolicy { queue });
  if (!plan || plan->regions.size() != 1U) return std::unexpected(failure);
  auto staging = graphics->CreateBuffer({
    .size_bytes = plan->total_bytes,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = kName,
    .allocation_budget = { .owner = renderer_.GetLightingAllocationBudget() },
  });
  if (!staging) return std::unexpected(failure);
  const auto& region = plan->regions.front();
  const auto source_pitch = static_cast<std::size_t>(data->view_nodes) * sizeof(float) * 2U;
  auto* mapped = static_cast<std::byte*>(staging->Map());
  if (mapped == nullptr) return std::unexpected(failure);
  auto destination = std::span(mapped, static_cast<std::size_t>(plan->total_bytes));
  for (std::uint32_t row = 0; row < data->roughness_nodes; ++row) {
    std::memcpy(destination.subspan(region.buffer_offset
        + static_cast<std::size_t>(row) * region.buffer_row_pitch, source_pitch).data(),
      data->energy.subspan(static_cast<std::size_t>(row) * source_pitch, source_pitch).data(),
      source_pitch);
  }
  staging->UnMap();
  auto recording = graphics->AcquireCommandRecorder(queue,
    "Vortex.Lighting.InitializeBrdfEnergy", graphics::SubmissionPolicy::kExplicit);
  if (!recording) return std::unexpected(failure);
  recording->BeginTrackingResourceState(*staging, graphics::ResourceStates::kGenericRead);
  recording->BeginTrackingResourceState(*texture_, graphics::ResourceStates::kCommon);
  recording->RequireResourceState(*texture_, graphics::ResourceStates::kCopyDest);
  recording->FlushBarriers();
  recording->CopyBufferToTexture(*staging, region, *texture_);
  recording->RequireResourceStateFinal(*texture_, graphics::ResourceStates::kShaderResource);
  if (!recording.Submit()) return std::unexpected(failure);
  graphics->GetDeferredReclaimer().RegisterDeferredAction(
    [owner = graphics.get(), resource = std::move(staging)] mutable -> void {
      owner->ForgetKnownResourceState(resource->GetNativeResource());
      resource.reset();
    });
  initialized_ = true;
  return {};
}

auto BrdfEnergyResources::Publish(LightingFrameBindings& bindings) const -> void
{
  if (initialized_) {
    bindings.brdf_energy_srv = slot_;
    bindings.brdf_model_revision = kBrdfModelRevision;
  }
}

} // namespace oxygen::vortex::lighting::internal
