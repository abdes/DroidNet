//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <new>
#include <span>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ManagedResource.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Environment/Internal/IblBrdfLookup.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Upload/ImmutableTextureUpload.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>

namespace oxygen::vortex::environment::internal {

IblBrdfResources::IblBrdfResources(Graphics& graphics)
  : graphics_(graphics)
  , owned_uploads_(
      std::make_unique<upload::UploadCoordinator>(observer_ptr { &graphics },
        upload::UploadPolicy {
          graphics.QueueKeyFor(graphics::QueueRole::kGraphics) }))
  , uploads_(owned_uploads_.get())
{
}

IblBrdfResources::IblBrdfResources(Renderer& renderer)
  : graphics_(*renderer.GetGraphics())
  , uploads_(&renderer.GetUploadCoordinator())
  , diagnostics_(&renderer.GetDiagnosticsService())
{
}

IblBrdfResources::~IblBrdfResources() = default;

auto IblBrdfResources::Prepare()
  -> std::expected<std::shared_ptr<const IblBrdfProduct>, IblProcessError>
{
  auto& registry = graphics_.GetResourceRegistry();
  if (product_) {
    if (!registry.RetainUse(product_->registration)) {
      return std::unexpected(IblProcessError::kBrdfUnavailable);
    }
    return product_;
  }
  try {
    const auto views = std::array { graphics::TextureViewRequest {
      .description = {
        .view_type = graphics::ResourceViewType::kTexture_SRV,
        .format = Format::kRG16UNorm,
        .dimension = TextureType::kTexture2D,
        .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      },
      .domain = bindless::generated::kTexturesDomain,
    }, };
    auto texture = registry.RegisterManagedTexture(
      graphics_.CreateTexture({
        .width = kIblBrdfWidth,
        .height = kIblBrdfHeight,
        .format = Format::kRG16UNorm,
        .texture_type = TextureType::kTexture2D,
        .debug_name = "IBL.BrdfLookup",
        .is_shader_resource = true,
        .initial_state = graphics::ResourceStates::kCommon,
      }),
      views);
    if (!texture) {
      return std::unexpected(IblProcessError::kAllocationFailed);
    }
    constexpr auto kRowBytes = kIblBrdfWidth * sizeof(IblBrdfTexel);
    const auto source = upload::UploadTextureSourceView {
      .subresources = { upload::UploadTextureSourceSubresource {
        .bytes = std::as_bytes(GetIblBrdfLookup()),
        .row_pitch = static_cast<std::uint32_t>(kRowBytes),
        .slice_pitch = static_cast<std::uint32_t>(kRowBytes * kIblBrdfHeight),
      }, },
    };
    auto prepared
      = uploads_->PrepareImmutableTexture2D(std::move(*texture), source);
    if (!prepared) {
      return std::unexpected(IblProcessError::kAllocationFailed);
    }
    const auto& destination = prepared->Destination();
    auto lease = registry.AcquireManaged(destination.registration.Identity());
    if (!lease) {
      return std::unexpected(IblProcessError::kBrdfUnavailable);
    }
    auto candidate = std::make_shared<IblBrdfProduct>();
    candidate->texture = destination.resource;
    candidate->registration = std::move(*lease);
    candidate->srv = destination.views.front().shader_visible_index;
    const auto queue = graphics_.QueueKeyFor(graphics::QueueRole::kGraphics);
    auto recording = graphics_.AcquireCommandRecorder(queue,
      "Vortex.Environment.IBL.BrdfUpload",
      graphics::SubmissionPolicy::kExplicit);
    if (!recording) {
      return std::unexpected(IblProcessError::kRecordingFailed);
    }
    bool accepted = false;
    const ScopeGuard timing([&] noexcept -> void {
      if (!accepted && diagnostics_) {
        diagnostics_->InvalidateIblTiming();
      }
    });
    if (diagnostics_) {
      diagnostics_->AttachIblTimelineCollector(*recording);
    }
    {
      const graphics::GpuEventScope upload_scope(*recording,
        "Vortex.Environment.IBL.BrdfUpload",
        profiling::ProfileGranularity::kTelemetry,
        profiling::ProfileCategory::kUpload);
      if (!prepared->Record(*recording)) {
        return std::unexpected(IblProcessError::kRecordingFailed);
      }
    }
    const auto submission = recording.SubmitWithReceipt();
    if (submission.outcome != graphics::SubmissionOutcome::kSubmitted
      || !submission.receipt) {
      return std::unexpected(IblProcessError::kSubmissionFailed);
    }
    candidate->producer = *submission.receipt;
    accepted = true;
    product_ = std::move(candidate);
    return product_;
  } catch (const std::bad_alloc&) {
    return std::unexpected(IblProcessError::kAllocationFailed);
  } catch (const std::exception&) {
    return std::unexpected(IblProcessError::kRecordingFailed);
  }
}
} // namespace oxygen::vortex::environment::internal
