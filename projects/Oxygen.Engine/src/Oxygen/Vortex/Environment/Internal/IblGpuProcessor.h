//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/Submission.h>
#include <Oxygen/Nexus/IndexReuse.h>
#include <Oxygen/Vortex/Environment/Types/IblCaptureLease.h>
#include <Oxygen/Vortex/Types/EnvironmentStaticData.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
}
namespace oxygen::graphics {
class Texture;
class Buffer;
class CommandRecorder;
class ResourceRegistry;
}

namespace oxygen::vortex {
class Renderer;
class DiagnosticsService;
namespace upload {
  class UploadCoordinator;
}
}

namespace oxygen::vortex::environment::internal {

enum class IblProcessError : std::uint8_t {
  kInvalidSource,
  kBrdfUnavailable,
  kAllocationFailed,
  kRecordingFailed,
  kSubmissionFailed,
  kPoolBusy,
  kClosed,
};

struct IblBrdfProduct {
  std::shared_ptr<graphics::Texture> texture;
  graphics::RegistrationLease registration;
  ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
  graphics::CompletionReceipt producer;
};

//! Renderer/device-owned immutable lookup; failed uploads remain retryable.
class IblBrdfResources final {
public:
  OXGN_VRTX_API explicit IblBrdfResources(Graphics& graphics);
  OXGN_VRTX_API explicit IblBrdfResources(Renderer& renderer);
  OXGN_VRTX_API ~IblBrdfResources();
  OXYGEN_MAKE_NON_COPYABLE(IblBrdfResources)
  OXYGEN_MAKE_NON_MOVABLE(IblBrdfResources)

  OXGN_VRTX_NDAPI auto Prepare()
    -> std::expected<std::shared_ptr<const IblBrdfProduct>, IblProcessError>;

private:
  Graphics& graphics_;
  std::unique_ptr<upload::UploadCoordinator> owned_uploads_;
  observer_ptr<upload::UploadCoordinator> uploads_;
  observer_ptr<DiagnosticsService> diagnostics_ { nullptr };
  std::shared_ptr<const IblBrdfProduct> product_;
};

struct IblProcessSettings {
  static constexpr std::uint32_t kDefaultFaceSize = 128U;
  std::uint32_t face_size { kDefaultFaceSize };
  float source_rotation_radians { 0.0F };
  //! Multiplies a cube source's radiance, e.g. its illuminance calibration.
  //! A sky capture carries its own Sky Sphere scale in its snapshot.
  float source_scale { 1.0F };
  bool lower_hemisphere_solid_color { true };
  std::array<float, 3> lower_hemisphere_color {};
  float lower_hemisphere_blend_alpha { 1.0F };
  //! Internal dispatch granularity in texels; zero records whole mip faces.
  std::uint32_t dispatch_tile_size { 0U };
};

//! Scene-global source. ProcessSky copies unit-exposure LUTs into its admitted
//! slot before capture; subsequent source writes must follow that submission.
//! A cross-queue producer supplies its receipt. Atmosphere-off needs no LUTs.
//! Immutable resident cube texture, its SRV and the lease that keeps both.
struct IblCubeView {
  std::shared_ptr<const graphics::Texture> texture;
  ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
  std::shared_ptr<const void> owner;
};

struct IblSkySource {
  //! A captured Sky Sphere sets `environment.sky_sphere` with the atmosphere
  //! disabled; a cubemap Sky Sphere also supplies `sky_sphere_cube`. Its
  //! `intensity` carries the illuminance calibration scale, never the display
  //! intensity.
  EnvironmentStaticData environment;
  EnvironmentViewData view;
  std::array<float, 3> origin {};
  std::shared_ptr<const graphics::Texture> sky_view;
  std::shared_ptr<const graphics::Buffer> distant_sky;
  IblCubeView sky_sphere_cube;
  graphics::CompletionReceipt producer;
};

//! Immutable submitted products. Each graphics-queue reader calls Attach to
//! retain generation contents, descriptors and allocations through completion.
struct IblGenerationOwner;

struct IblGpuProducts {
  std::shared_ptr<const IblBrdfProduct> brdf;
  std::shared_ptr<graphics::Texture> processed_cube;
  std::shared_ptr<graphics::Texture> specular_cube;
  std::shared_ptr<graphics::Texture> processed_half_cube;
  std::shared_ptr<graphics::Texture> specular_half_cube;
  std::shared_ptr<graphics::Buffer> diffuse_sh;
  std::shared_ptr<graphics::Buffer> metadata;
  ShaderVisibleIndex processed_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex specular_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex diffuse_sh_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex metadata_srv { kInvalidShaderVisibleIndex };
  std::uint32_t maximum_mip { 0U };
  std::uint32_t revision { 0U };
  graphics::CompletionReceipt producer;
  std::vector<graphics::RegistrationLease> registrations;
  nexus::VersionedIndex<std::uint32_t> slot;
  std::shared_ptr<IblGenerationOwner> allocation;

  [[nodiscard]] OXGN_VRTX_API auto Attach(graphics::CommandRecorder& recorder,
    graphics::ResourceRegistry& registry) const -> bool;
  [[nodiscard]] OXGN_VRTX_API auto AcquireCapture() const
    -> Result<IblCaptureLease, IblCaptureError>;
};

struct IblGpuJob;
class IblWorkBudget;

//! Immutable dispatch description used by the scene's work-budget owner.
struct IblGpuDispatch {
  const char* shader {};
  std::uint32_t work_index {};
  std::array<std::uint32_t, 3> groups {};
  std::uint32_t output_size {};
  //! Active texels, GGX samples or reduction inputs, according to the shader.
  std::uint64_t work_units {};
  [[nodiscard]] OXGN_VRTX_API auto CanShareBatchWith(
    const IblGpuDispatch& other) const noexcept -> bool;
};

struct IblGpuAdvance {
  std::uint32_t recorded_dispatches {};
  std::uint32_t remaining_dispatches {};
  //! Non-null only after the complete producer set is accepted for submission.
  std::shared_ptr<const IblGpuProducts> products;
};

//! One scene's bounded product storage. Process/Close run on the renderer
//! thread; returned generations and previously admitted recordings may outlive
//! it.
class IblGpuProcessor final {
public:
  static constexpr std::uint32_t kNormalSlots
    = frame::kFramesInFlight.get() + 2U;
  static constexpr std::uint32_t kMaximumCaptureGenerations = 2U;
  static constexpr std::uint32_t kMaximumSlots
    = kNormalSlots + kMaximumCaptureGenerations;
  struct Stats {
    std::uint32_t capacity {};
    std::uint32_t available {};
    std::uint32_t allocated {};
    std::uint64_t storage_creations {};
    nexus::IndexReuseTelemetry reuse;
    std::uint32_t normal_capacity {};
    std::uint32_t normal_in_use {};
    std::uint32_t capture_capacity {};
    std::uint32_t captured_generations {};
  };

  OXGN_VRTX_API explicit IblGpuProcessor(
    Graphics& graphics, std::uint32_t capacity = kMaximumSlots);
  //! Production owner also supplies the existing GPU timeline collector.
  OXGN_VRTX_API explicit IblGpuProcessor(
    Renderer& renderer, std::uint32_t capacity = kMaximumSlots);
  OXGN_VRTX_API ~IblGpuProcessor();
  OXYGEN_MAKE_NON_COPYABLE(IblGpuProcessor)
  OXYGEN_MAKE_NON_MOVABLE(IblGpuProcessor)

  //! Immutable source with a matching managed registration. Supply a receipt
  //! for cross-queue input; otherwise writes must already precede this call on
  //! Graphics. Publication follows successful submission, not a CPU fence wait.
  [[nodiscard]] OXGN_VRTX_API auto Process(
    const std::shared_ptr<graphics::Texture>& source,
    const graphics::RegistrationLease& source_registration,
    const std::shared_ptr<const IblBrdfProduct>& brdf,
    const IblProcessSettings& settings, std::uint32_t revision,
    graphics::CompletionReceipt source_producer = {})
    -> std::expected<std::shared_ptr<const IblGpuProducts>, IblProcessError>;
  [[nodiscard]] OXGN_VRTX_API auto ProcessSky(const IblSkySource& source,
    const std::shared_ptr<const IblBrdfProduct>& brdf,
    const IblProcessSettings& settings, std::uint32_t revision)
    -> std::expected<std::shared_ptr<const IblGpuProducts>, IblProcessError>;
  //! Consumes an immutable resident texture lease (e.g. TextureBinder). The
  //! owner retains the supplied SRV and texture through source processing.
  [[nodiscard]] OXGN_VRTX_API auto ProcessCubeView(
    const std::shared_ptr<const graphics::Texture>& source,
    ShaderVisibleIndex srv, std::shared_ptr<const void> owner,
    const std::shared_ptr<const IblBrdfProduct>& brdf,
    const IblProcessSettings& settings, std::uint32_t revision)
    -> std::expected<std::shared_ptr<const IblGpuProducts>, IblProcessError>;
  //! Reserve a generation and submit its frozen sky inputs/invalid metadata.
  [[nodiscard]] OXGN_VRTX_API auto BeginSky(const IblSkySource& source,
    const std::shared_ptr<const IblBrdfProduct>& brdf,
    const IblProcessSettings& settings, std::uint32_t revision)
    -> std::expected<std::shared_ptr<IblGpuJob>, IblProcessError>;
  //! Retains the registered source; its texels must stay immutable until the
  //! job's last submitted read completes. BeginSky instead freezes mutable
  //! LUTs.
  [[nodiscard]] OXGN_VRTX_API auto Begin(
    const std::shared_ptr<graphics::Texture>& source,
    const graphics::RegistrationLease& source_registration,
    const std::shared_ptr<const IblBrdfProduct>& brdf,
    const IblProcessSettings& settings, std::uint32_t revision,
    graphics::CompletionReceipt source_producer = {})
    -> std::expected<std::shared_ptr<IblGpuJob>, IblProcessError>;
  //! Retains the immutable resident cube and descriptor owner across batches.
  [[nodiscard]] OXGN_VRTX_API auto BeginCubeView(
    const std::shared_ptr<const graphics::Texture>& source,
    ShaderVisibleIndex srv, std::shared_ptr<const void> owner,
    const std::shared_ptr<const IblBrdfProduct>& brdf,
    const IblProcessSettings& settings, std::uint32_t revision)
    -> std::expected<std::shared_ptr<IblGpuJob>, IblProcessError>;
  [[nodiscard]] OXGN_VRTX_API auto PendingDispatches(const IblGpuJob& job) const
    -> std::span<const IblGpuDispatch>;
  [[nodiscard]] OXGN_VRTX_API auto Advance(
    const std::shared_ptr<IblGpuJob>& job, std::uint32_t dispatch_limit)
    -> std::expected<IblGpuAdvance, IblProcessError>;
  OXGN_VRTX_API auto Close() noexcept -> void;
  [[nodiscard]] OXGN_VRTX_API auto IsOpen() const -> bool;
  OXGN_VRTX_API auto SetTimingContext(std::shared_ptr<IblWorkBudget> budget,
    frame::SequenceNumber sequence) -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetStats() const -> Stats;

private:
  auto ProcessSource(const std::shared_ptr<const graphics::Texture>& source,
    const graphics::RegistrationLease& source_registration,
    const std::shared_ptr<const IblBrdfProduct>& brdf,
    const IblProcessSettings& settings, std::uint32_t revision,
    graphics::CompletionReceipt source_producer, const IblSkySource* sky,
    ShaderVisibleIndex resident_srv = kInvalidShaderVisibleIndex,
    std::shared_ptr<const void> resident_owner = {})
    -> std::expected<std::shared_ptr<const IblGpuProducts>, IblProcessError>;
  auto PrepareSource(const std::shared_ptr<const graphics::Texture>& source,
    const graphics::RegistrationLease& source_registration,
    const std::shared_ptr<const IblBrdfProduct>& brdf,
    const IblProcessSettings& settings, std::uint32_t revision,
    graphics::CompletionReceipt source_producer, const IblSkySource* sky,
    ShaderVisibleIndex resident_srv = kInvalidShaderVisibleIndex,
    std::shared_ptr<const void> resident_owner = {})
    -> std::expected<std::shared_ptr<IblGpuJob>, IblProcessError>;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex::environment::internal
