//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ManagedResource.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/ImmutableTextureUpload.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/Upload/UploadPlanner.h>

namespace oxygen::vortex::upload {
namespace {
  auto RegistrationFailure(const graphics::RegistrationError error)
    -> UploadError
  {
    switch (error) {
    case graphics::RegistrationError::kAllocationFailed:
      return UploadError::kResourceAllocFailed;
    case graphics::RegistrationError::kClosed:
      return UploadError::kDeviceLost;
    case graphics::RegistrationError::kWrongBackend:
    case graphics::RegistrationError::kStaleRegistration:
    case graphics::RegistrationError::kOwnershipConflict:
      return UploadError::kInvalidRequest;
    }
    return UploadError::kInvalidRequest;
  }
} // namespace

ImmutableTextureUpload::ImmutableTextureUpload(std::weak_ptr<Graphics> graphics,
  graphics::ManagedTexture destination, graphics::ManagedBuffer staging,
  std::vector<graphics::TextureUploadRegion> regions)
  : graphics_(std::move(graphics))
  , destination_(std::move(destination))
  , staging_(std::move(staging))
  , regions_(std::move(regions))
{
}

auto UploadCoordinator::PrepareImmutableTexture2D(
  graphics::ManagedTexture destination, const UploadTextureSourceView& source,
  const std::span<const UploadSubresource> subresources)
  -> Result<ImmutableTextureUpload, UploadError>
{
  if (shutting_down_.load()) {
    return Err(UploadError::kTrackerShutdown);
  }
  if (!destination.resource || !destination.registration) {
    return Err(UploadError::kInvalidRequest);
  }
  const auto& description = destination.resource->GetDescriptor();
  if ((description.texture_type != TextureType::kTexture2D
        && description.texture_type != TextureType::kTexture2DArray)
    || description.sample_count != 1U || !description.is_shader_resource
    || description.initial_state != graphics::ResourceStates::kCommon) {
    return Err(UploadError::kInvalidRequest);
  }
  auto& registry = gfx_->GetResourceRegistry();
  if (registry.InspectManagedIdentity(*destination.resource)
    != destination.registration.Identity()) {
    return Err(UploadError::kInvalidRequest);
  }
  if (const auto use = registry.RetainUse(destination.registration); !use) {
    return Err(RegistrationFailure(use.error()));
  }
  auto failure = UploadError::kInvalidRequest;
  try {
    // The native buffer-to-texture recorder copies full subresources using
    // its canonical footprint. Immutable LUT initialization does not support
    // boxed updates or caller-selected destination pitches.
    for (const auto& subresource : subresources) {
      if (subresource.x != 0U || subresource.y != 0U || subresource.z != 0U
        || subresource.width != 0U || subresource.height != 0U
        || subresource.depth != 0U) {
        return Err(UploadError::kInvalidRequest);
      }
    }
    auto initialization_policy = policy_;
    initialization_policy.alignment = UploadPolicy::AlignmentPolicy {};
    auto plan = UploadPlanner::PlanTexture2D(
      { .dst = destination.resource }, subresources, initialization_policy);
    if (!plan) {
      return Err(plan.error());
    }
    const auto expected_regions
      = subresources.empty() ? 1U : subresources.size();
    if (plan->regions.size() != expected_regions
      || source.subresources.size() != expected_regions) {
      return Err(UploadError::kInvalidRequest);
    }
    failure = UploadError::kStagingAllocFailed;
    auto staging = registry.RegisterManagedBuffer(gfx_->CreateBuffer({
      .size_bytes = plan->total_bytes,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = std::string(destination.resource->GetName()),
      .allocation_budget = description.allocation_budget,
    }));
    if (!staging) {
      return Err(UploadError::kStagingAllocFailed);
    }
    failure = UploadError::kStagingMapFailed;
    auto* const mapped = static_cast<std::byte*>(staging->resource->Map());
    if (mapped == nullptr) {
      return Err(UploadError::kStagingMapFailed);
    }
    {
      const ScopeGuard unmap(
        [&] noexcept -> void { staging->resource->UnMap(); });
      failure = UploadError::kInvalidRequest;
      if (!plan->Pack2D(description, source,
            std::span(mapped, static_cast<std::size_t>(plan->total_bytes)),
            policy_.filler)) {
        return Err(UploadError::kInvalidRequest);
      }
    }
    return Ok(ImmutableTextureUpload(gfx_->weak_from_this(),
      std::move(destination), std::move(*staging), std::move(plan->regions)));
  } catch (const graphics::AllocationBudgetExceeded&) {
    return Err(UploadError::kBudgetExceeded);
  } catch (const std::bad_alloc&) {
    return Err(UploadError::kStagingAllocFailed);
  } catch (const std::exception&) {
    return Err(failure);
  }
}

auto ImmutableTextureUpload::Record(graphics::CommandRecorder& recorder)
  -> Result<void, UploadError>
{
  if (recorded_ || !destination_.resource || !staging_.resource) {
    return Err(UploadError::kInvalidRequest);
  }
  const auto queue = recorder.GetTargetQueue();
  if (!queue || queue->GetQueueRole() != graphics::QueueRole::kGraphics) {
    return Err(UploadError::kInvalidRequest);
  }
  const auto graphics = graphics_.lock();
  if (!graphics) {
    return Err(UploadError::kDeviceLost);
  }
  if (queue
    != graphics->GetCommandQueue(
      graphics->QueueKeyFor(graphics::QueueRole::kGraphics))) {
    return Err(UploadError::kInvalidRequest);
  }
  try {
    auto& registry = graphics->GetResourceRegistry();
    if (const auto retained
      = recorder.RetainRegistration(registry, destination_.registration);
      !retained) {
      return Err(RegistrationFailure(retained.error()));
    }
    if (const auto retained
      = recorder.RetainRegistration(registry, staging_.registration);
      !retained) {
      return Err(RegistrationFailure(retained.error()));
    }
    recorded_ = true;
    recorder.BeginTrackingResourceState(
      *staging_.resource, graphics::ResourceStates::kGenericRead);
    recorder.BeginTrackingResourceState(
      *destination_.resource, graphics::ResourceStates::kCommon);
    recorder.RequireResourceState(
      *destination_.resource, graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyBufferToTexture(
      *staging_.resource, std::span(regions_), *destination_.resource);
    recorder.RequireResourceStateFinal(
      *destination_.resource, graphics::ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    return Result<void, UploadError>::Ok();
  } catch (const std::exception&) {
    return Err(UploadError::kRecordingFailed);
  }
}
} // namespace oxygen::vortex::upload
