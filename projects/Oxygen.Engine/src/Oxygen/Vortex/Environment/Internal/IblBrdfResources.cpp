//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <span>
#include <utility>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Vortex/Environment/Internal/IblBrdfLookup.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadPlanner.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>

namespace oxygen::vortex::environment::internal {

IblBrdfResources::IblBrdfResources(Graphics& graphics)
  : graphics_(graphics)
{
}

IblBrdfResources::~IblBrdfResources() = default;

auto IblBrdfResources::Prepare()
  -> std::expected<std::shared_ptr<const IblBrdfProduct>, IblProcessError>
{
  auto& registry = graphics_.GetResourceRegistry();
  if (product_) {
    // A backend close invalidates acquisition even if an external lease still
    // owns the native allocation. Do not advertise it to a new generation.
    if (!registry.RetainUse(product_->registration))
      return std::unexpected(IblProcessError::kBrdfUnavailable);
    return product_;
  }
  try {
    auto candidate = std::make_shared<IblBrdfProduct>();
    candidate->texture = graphics_.CreateTexture({ .width = kIblBrdfWidth,
      .height = kIblBrdfHeight,
      .format = Format::kRG16UNorm,
      .texture_type = TextureType::kTexture2D,
      .debug_name = "IBL.BrdfLookup",
      .is_shader_resource = true,
      .initial_state = graphics::ResourceStates::kCommon });
    auto registration = registry.RegisterManaged(candidate->texture);
    if (!registration)
      return std::unexpected(IblProcessError::kAllocationFailed);
    candidate->registration = std::move(*registration);
    const auto view
      = registry.AcquireManagedView<graphics::Texture>(candidate->registration,
        { .view_type = graphics::ResourceViewType::kTexture_SRV,
          .format = Format::kRG16UNorm,
          .dimension = TextureType::kTexture2D,
          .sub_resources = graphics::TextureSubResourceSet::EntireTexture() },
        bindless::generated::kTexturesDomain);
    if (!view)
      return std::unexpected(IblProcessError::kAllocationFailed);
    candidate->srv = view->shader_visible_index;
    const auto queue = graphics_.QueueKeyFor(graphics::QueueRole::kGraphics);
    const auto plan = upload::UploadPlanner::PlanTexture2D(
      { .dst = candidate->texture }, {}, upload::UploadPolicy { queue });
    if (!plan || plan->regions.size() != 1U)
      return std::unexpected(IblProcessError::kAllocationFailed);
    auto staging = graphics_.CreateBuffer({ .size_bytes = plan->total_bytes,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = "IBL.BrdfUpload" });
    auto staging_registration = registry.RegisterManaged(staging);
    if (!staging_registration)
      return std::unexpected(IblProcessError::kAllocationFailed);
    const auto data = GetIblBrdfLookup();
    const auto& region = plan->regions.front();
    for (auto row = 0U; row < kIblBrdfHeight; ++row) {
      staging->Update(data.data() + row * kIblBrdfWidth,
        kIblBrdfWidth * sizeof(IblBrdfTexel),
        region.buffer_offset
          + static_cast<std::uint64_t>(row) * region.buffer_row_pitch);
    }
    auto recording = graphics_.AcquireCommandRecorder(queue,
      "Vortex.Environment.IBL.BrdfUpload",
      graphics::SubmissionPolicy::kExplicit);
    if (!recording
      || !recording->RetainRegistration(registry, candidate->registration)
      || !recording->RetainRegistration(registry, *staging_registration))
      return std::unexpected(IblProcessError::kRecordingFailed);
    recording->BeginTrackingResourceState(
      *staging, graphics::ResourceStates::kGenericRead);
    recording->BeginTrackingResourceState(
      *candidate->texture, graphics::ResourceStates::kCommon);
    recording->RequireResourceState(
      *candidate->texture, graphics::ResourceStates::kCopyDest);
    recording->FlushBarriers();
    recording->CopyBufferToTexture(*staging, region, *candidate->texture);
    recording->RequireResourceStateFinal(
      *candidate->texture, graphics::ResourceStates::kShaderResource);
    const auto submission = recording.SubmitWithReceipt();
    if (submission.outcome != graphics::SubmissionOutcome::kSubmitted
      || !submission.receipt)
      return std::unexpected(IblProcessError::kSubmissionFailed);
    candidate->producer = *submission.receipt;
    product_ = std::move(candidate);
    return product_;
  } catch (const std::bad_alloc&) {
    return std::unexpected(IblProcessError::kAllocationFailed);
  } catch (const std::exception&) {
    return std::unexpected(IblProcessError::kRecordingFailed);
  }
}

} // namespace oxygen::vortex::environment::internal
