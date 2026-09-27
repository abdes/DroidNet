//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <utility>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ManagedResource.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Submission.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Lighting/Internal/BrdfEnergyData.h>
#include <Oxygen/Vortex/Lighting/Internal/BrdfEnergyResources.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/Upload/ImmutableTextureUpload.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

namespace oxygen::vortex::lighting::internal {

BrdfEnergyResources::BrdfEnergyResources(Renderer& renderer)
  : renderer_(renderer)
{
}

BrdfEnergyResources::~BrdfEnergyResources() = default;

auto BrdfEnergyResources::Prepare()
  -> std::expected<std::shared_ptr<const BrdfEnergyProduct>,
    LightingPreparationFailure>
{
  const auto graphics = renderer_.GetGraphics();
  const auto failure = LightingPreparationFailure {
    .error = LightingPreparationError::kAllocationFailed,
  };
  if (!graphics) {
    return std::unexpected(failure);
  }
  auto& registry = graphics->GetResourceRegistry();
  if (product_) {
    if (!registry.RetainUse(product_->allocation.registration)) {
      return std::unexpected(failure);
    }
    return product_;
  }
  const auto data = GetBrdfEnergyData();
  if (!data) {
    return std::unexpected(LightingPreparationFailure {
      .error = LightingPreparationError::kMissingBrdfData,
    });
  }
  const auto views = std::array { graphics::TextureViewRequest {
    .description = {
      .view_type = graphics::ResourceViewType::kTexture_SRV,
      .format = Format::kRG32Float,
      .dimension = TextureType::kTexture2D,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    },
    .domain = bindless::generated::kTexturesDomain,
  }, };
  auto allocation = registry.RegisterManagedTexture(
    graphics->CreateTexture({
      .width = data->view_nodes,
      .height = data->roughness_nodes,
      .format = Format::kRG32Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = "LightingService.BrdfEnergy",
      .is_shader_resource = true,
      .initial_state = graphics::ResourceStates::kCommon,
      .allocation_budget = { .owner = renderer_.GetLightingAllocationBudget() },
    }),
    views);
  if (!allocation) {
    return std::unexpected(failure);
  }
  const auto row_bytes = data->view_nodes * sizeof(float) * 2U;
  const auto source = upload::UploadTextureSourceView {
    .subresources = { upload::UploadTextureSourceSubresource {
      .bytes = data->energy,
      .row_pitch = static_cast<std::uint32_t>(row_bytes),
      .slice_pitch = static_cast<std::uint32_t>(row_bytes * data->roughness_nodes),
    }, },
  };
  // Cache only the immutable product. The upload owns its staging until the
  // recording has retained it; no frame-based retirement callback is needed.
  auto candidate = std::make_shared<BrdfEnergyProduct>();
  auto prepared = renderer_.GetUploadCoordinator().PrepareImmutableTexture2D(
    std::move(*allocation), source);
  if (!prepared) {
    return std::unexpected(failure);
  }
  const auto& destination = prepared->Destination();
  candidate->allocation.resource = destination.resource;
  candidate->allocation.registration = destination.registration;
  candidate->allocation.views = destination.views;
  auto recording = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(graphics::QueueRole::kGraphics),
    "Vortex.Lighting.InitializeBrdfEnergy",
    graphics::SubmissionPolicy::kExplicit);
  if (!recording || !prepared->Record(*recording)) {
    return std::unexpected(failure);
  }
  const auto submission = recording.SubmitWithReceipt();
  if (submission.outcome != graphics::SubmissionOutcome::kSubmitted
    || !submission.receipt) {
    return std::unexpected(failure);
  }
  candidate->producer = *submission.receipt;
  product_ = std::move(candidate);
  return product_;
}

auto BrdfEnergyProduct::Publish(LightingFrameBindings& bindings) const -> void
{
  bindings.brdf_energy_srv = allocation.views.front().shader_visible_index;
  bindings.brdf_model_revision = kBrdfModelRevision;
}

auto BrdfEnergyProduct::Attach(graphics::ResourceRegistry& registry,
  graphics::CommandRecorder& recorder) const -> bool
{
  if (recorder.RetainsRegistration(allocation.registration.Identity())) {
    return true;
  }
  if (!recorder.RetainRegistration(registry, allocation.registration)) {
    return false;
  }
  recorder.RecordDependency(producer);
  return true;
}
} // namespace oxygen::vortex::lighting::internal
