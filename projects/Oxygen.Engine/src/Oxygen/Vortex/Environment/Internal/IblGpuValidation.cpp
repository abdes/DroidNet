//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <utility>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuValidation.h>

namespace oxygen::vortex::environment::internal {
namespace {
  auto SameProduct(const IblGpuValidation::Observation& observation,
    const IblGpuProducts& products) -> bool
  {
    return observation.slot == products.slot
      && observation.revision == products.revision;
  }

  auto Ready(const IblProductMetadata& data, const std::uint32_t revision)
    -> bool
  {
    // Same readiness predicate as
    // Contracts/Environment/IblProductMetadata.hlsli.
    return revision != 0U && data.product_revision == revision
      && data.processing_flags == (kIblProductFinite | kIblProductComplete)
      && std::isfinite(data.source_radiance_scale)
      && data.source_radiance_scale >= 1.0F
      && std::isfinite(data.average_brightness)
      && data.average_brightness >= 0.0F;
  }
}

IblGpuValidation::IblGpuValidation(std::shared_ptr<Graphics> graphics)
  : graphics_(std::move(graphics))
{
}
IblGpuValidation::~IblGpuValidation() = default;

auto IblGpuValidation::Remember(const Observation& observation) noexcept -> void
{
  if (!latest_ || observation.order >= latest_->order)
    latest_ = observation;
  if (observation.status == SkyLightGpuValidation::kInvalid
    && observation.order > last_failed_order_) {
    last_failed_revision_ = observation.revision;
    last_failed_order_ = observation.order;
  }
  if (observation.status == SkyLightGpuValidation::kUnavailable
    && last_submitted_ && last_submitted_->order == observation.order)
    last_submitted_.reset();
}

auto IblGpuValidation::Poll() noexcept -> Observations
{
  auto completed = Observations {};
  for (std::size_t index = 0U; index < pending_.size(); ++index) {
    auto& pending = pending_[index];
    if (!pending.request)
      continue;
    auto observed = *pending.request;
    observed.status = SkyLightGpuValidation::kUnavailable;
    try {
      const auto ready = pending.readback->IsReady();
      if (ready && !*ready)
        continue;
      if (ready) {
        {
          const auto mapped = pending.readback->TryMap();
          if (mapped && mapped->Bytes().size() == sizeof(IblProductMetadata)) {
            std::memcpy(&observed.metadata, mapped->Bytes().data(),
              sizeof(observed.metadata));
            observed.status = Ready(observed.metadata, observed.revision)
              ? SkyLightGpuValidation::kValid
              : SkyLightGpuValidation::kInvalid;
          }
        }
        if (!pending.readback->ResetForReuse())
          pending.readback.reset();
      } else
        pending.readback.reset();
    } catch (const std::exception&) {
      pending.readback.reset();
    }
    pending.request.reset();
    Remember(observed);
    completed[index] = observed;
  }
  return completed;
}

auto IblGpuValidation::Request(const IblGpuProducts& products,
  DiagnosticsService& diagnostics) noexcept -> void
{
  if (last_submitted_ && SameProduct(*last_submitted_, products))
    return;
  auto free = std::ranges::find_if(
    pending_, [](const auto& pending) { return !pending.request; });
  if (free == pending_.end())
    return;
  auto request = Observation {
    .slot = products.slot, .revision = products.revision, .order = ++next_order_
  };
  bool accepted = false;
  const ScopeGuard failure([&]() noexcept {
    if (!accepted) {
      Remember(request);
      free->readback.reset();
    }
  });
  try {
    const auto manager = graphics_->GetReadbackManager();
    if (!manager)
      return;
    if (!free->readback)
      free->readback = manager->CreateBufferReadback("IBL.MetadataDiagnostic");
    if (!free->readback)
      return;
    auto recording = graphics_->AcquireCommandRecorder(
      graphics_->QueueKeyFor(graphics::QueueRole::kGraphics),
      "Vortex.Diagnostics.IBL.Metadata", graphics::SubmissionPolicy::kExplicit);
    if (!recording)
      return;
    const ScopeGuard timing([&]() noexcept {
      if (!accepted)
        diagnostics.InvalidateIblTiming();
    });
    diagnostics.AttachGpuTimelineCollector(*recording);
    {
      const graphics::GpuEventScope scope(*recording,
        "Vortex.Diagnostics.IBL.Metadata",
        profiling::ProfileGranularity::kTelemetry,
        profiling::ProfileCategory::kGeneral);
      if (!products.Attach(*recording, graphics_->GetResourceRegistry())
        || !free->readback->EnqueueCopy(
          *recording, *products.metadata, { 0U, sizeof(IblProductMetadata) }))
        return;
      recording->RequireResourceStateFinal(
        *products.metadata, graphics::ResourceStates::kShaderResource);
      recording->FlushBarriers();
    }
    const auto submission = recording.SubmitWithReceipt();
    if (submission.outcome != graphics::SubmissionOutcome::kSubmitted
      || !submission.receipt)
      return;
    accepted = true;
    request.status = SkyLightGpuValidation::kPending;
    free->request = request;
    last_submitted_ = request;
  } catch (const std::exception&) {
  }
}

auto IblGpuValidation::Inspect(const IblGpuProducts& products,
  const bool enabled) const -> SkyLightRuntimeState
{
  auto result = SkyLightRuntimeState { .last_failed_gpu_revision
    = last_failed_revision_ };
  if (latest_ && SameProduct(*latest_, products)) {
    result.gpu_validation = latest_->status;
    if (latest_->status == SkyLightGpuValidation::kValid
      || latest_->status == SkyLightGpuValidation::kInvalid)
      result.validated_revision = latest_->revision;
    if (latest_->status == SkyLightGpuValidation::kValid) {
      result.source_radiance_scale = latest_->metadata.source_radiance_scale;
      result.average_brightness = latest_->metadata.average_brightness;
    }
  } else
    result.gpu_validation = enabled ? SkyLightGpuValidation::kPending
                                    : SkyLightGpuValidation::kNotRequested;
  for (const auto& pending : pending_)
    if (pending.request && SameProduct(*pending.request, products))
      result.gpu_validation = SkyLightGpuValidation::kPending;
  return result;
}
} // namespace oxygen::vortex::environment::internal
