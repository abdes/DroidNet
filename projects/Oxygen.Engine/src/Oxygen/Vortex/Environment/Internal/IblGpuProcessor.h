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
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/Submission.h>
#include <Oxygen/Nexus/IndexReuse.h>
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
  OXGN_VRTX_API ~IblBrdfResources();
  IblBrdfResources(const IblBrdfResources&) = delete;
  auto operator=(const IblBrdfResources&) -> IblBrdfResources& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Prepare()
    -> std::expected<std::shared_ptr<const IblBrdfProduct>, IblProcessError>;

private:
  Graphics& graphics_;
  std::shared_ptr<const IblBrdfProduct> product_;
};

struct IblProcessSettings {
  std::uint32_t face_size { 128U };
  float source_rotation_radians { 0.0F };
  bool lower_hemisphere_solid_color { true };
  std::array<float, 3> lower_hemisphere_color {};
  float lower_hemisphere_blend_alpha { 1.0F };
};

//! Immutable submitted products. Each graphics-queue reader calls Attach to
//! retain generation contents, descriptors and allocations through completion.
struct IblGenerationOwner;

struct IblGpuProducts {
  std::shared_ptr<const IblBrdfProduct> brdf;
  std::shared_ptr<graphics::Texture> processed_cube;
  std::shared_ptr<graphics::Texture> specular_cube;
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
};

//! One scene's bounded product storage. Process/Close run on the renderer
//! thread; returned generations and previously admitted recordings may outlive
//! it.
class IblGpuProcessor final {
public:
  static constexpr std::uint32_t kMaximumSlots
    = frame::kFramesInFlight.get() + 4U;
  struct Stats {
    std::uint32_t capacity {};
    std::uint32_t available {};
    std::uint32_t allocated {};
    std::uint64_t storage_creations {};
    nexus::IndexReuseTelemetry reuse;
  };

  OXGN_VRTX_API explicit IblGpuProcessor(
    Graphics& graphics, std::uint32_t capacity = kMaximumSlots);
  OXGN_VRTX_API ~IblGpuProcessor();
  IblGpuProcessor(const IblGpuProcessor&) = delete;
  auto operator=(const IblGpuProcessor&) -> IblGpuProcessor& = delete;

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
  OXGN_VRTX_API auto Close() noexcept -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetStats() const -> Stats;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex::environment::internal
