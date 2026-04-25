//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>

namespace oxygen::vortex {

namespace {

  constexpr auto kActiveGBufferCount
    = static_cast<std::uint32_t>(GBufferIndex::kActiveCount);

  auto GBufferName(const GBufferIndex index) -> const char*
  {
    switch (index) {
    case GBufferIndex::kNormal:
      return "GBufferNormal";
    case GBufferIndex::kMaterial:
      return "GBufferMaterial";
    case GBufferIndex::kBaseColor:
      return "GBufferBaseColor";
    case GBufferIndex::kCustomData:
      return "GBufferCustomData";
    case GBufferIndex::kShadowFactors:
      return "GBufferShadowFactors";
    case GBufferIndex::kWorldTangent:
      return "GBufferWorldTangent";
    case GBufferIndex::kCount:
      break;
    }
    return "UnknownGBuffer";
  }

  auto GBufferFormat(const GBufferIndex index) -> Format
  {
    switch (index) {
    case GBufferIndex::kNormal:
      return Format::kR10G10B10A2UNorm;
    case GBufferIndex::kMaterial:
      return Format::kRGBA8UNorm;
    case GBufferIndex::kBaseColor:
      return Format::kRGBA8UNormSRGB;
    case GBufferIndex::kCustomData:
      return Format::kRGBA8UNorm;
    case GBufferIndex::kShadowFactors:
    case GBufferIndex::kWorldTangent:
    case GBufferIndex::kCount:
      break;
    }
    return Format::kUnknown;
  }

} // namespace

void SceneTextureBindings::Invalidate() noexcept
{
  scene_color_srv = kInvalidIndex;
  scene_depth_srv = kInvalidIndex;
  partial_depth_srv = kInvalidIndex;
  velocity_srv = kInvalidIndex;
  stencil_srv = kInvalidIndex;
  custom_depth_srv = kInvalidIndex;
  custom_stencil_srv = kInvalidIndex;
  gbuffer_srvs.fill(kInvalidIndex);
  scene_color_uav = kInvalidIndex;
  velocity_uav = kInvalidIndex;
  valid_flags = 0;
}

void SceneTextureExtracts::Reset() noexcept
{
  resolved_scene_color = {};
  resolved_scene_depth = {};
  prev_scene_depth = {};
  prev_velocity = {};
}

SceneTextures::SceneTextures(Graphics& gfx, const SceneTexturesConfig& config)
  : gfx_(gfx)
  , config_(config)
{
  ValidateConfig(config_);
  AllocateTextures();
}

SceneTextures::~SceneTextures() { ReleaseTextures(); }

auto SceneTextures::GetSceneColor() const -> graphics::Texture&
{
  return RequireTexture(scene_color_, "SceneColor");
}

auto SceneTextures::GetSceneDepth() const -> graphics::Texture&
{
  return RequireTexture(scene_depth_, "SceneDepth");
}

auto SceneTextures::GetPartialDepth() const -> graphics::Texture&
{
  return RequireTexture(partial_depth_, "PartialDepth");
}

auto SceneTextures::GetSceneColorResource() const
  -> const std::shared_ptr<graphics::Texture>&
{
  return scene_color_.resource;
}

auto SceneTextures::GetSceneDepthResource() const
  -> const std::shared_ptr<graphics::Texture>&
{
  return scene_depth_.resource;
}

auto SceneTextures::GetStencil() const -> SceneTextureAspectView
{
  return {
    .texture = scene_depth_.resource.get(),
    .aspect = SceneTextureAspectView::Aspect::kStencil,
  };
}

auto SceneTextures::GetGBuffer(const GBufferIndex index) const
  -> graphics::Texture&
{
  const auto raw_index = static_cast<std::size_t>(index);
  CHECK_F(raw_index < gbuffers_.size(),
    "SceneTextures: {} is outside the supported Vortex GBuffer range",
    GBufferName(index));
  CHECK_F(gbuffers_.at(raw_index).resource != nullptr,
    "SceneTextures: {} is not allocated in the current Vortex contract",
    GBufferName(index));
  return *gbuffers_.at(raw_index).resource;
}

auto SceneTextures::GetGBufferResource(const GBufferIndex index) const
  -> const std::shared_ptr<graphics::Texture>&
{
  const auto raw_index = static_cast<std::size_t>(index);
  CHECK_F(raw_index < gbuffers_.size(),
    "SceneTextures: {} is outside the supported Vortex GBuffer range",
    GBufferName(index));
  CHECK_F(gbuffers_.at(raw_index).resource != nullptr,
    "SceneTextures: {} is not allocated in the current Vortex contract",
    GBufferName(index));
  return gbuffers_.at(raw_index).resource;
}

auto SceneTextures::GetGBufferNormal() const -> graphics::Texture&
{
  return GetGBuffer(GBufferIndex::kNormal);
}

auto SceneTextures::GetGBufferMaterial() const -> graphics::Texture&
{
  return GetGBuffer(GBufferIndex::kMaterial);
}

auto SceneTextures::GetGBufferBaseColor() const -> graphics::Texture&
{
  return GetGBuffer(GBufferIndex::kBaseColor);
}

auto SceneTextures::GetGBufferCustomData() const -> graphics::Texture&
{
  return GetGBuffer(GBufferIndex::kCustomData);
}

auto SceneTextures::GetGBufferCount() const noexcept -> std::uint32_t
{
  return config_.gbuffer_count;
}

auto SceneTextures::GetVelocity() const -> graphics::Texture*
{
  return velocity_.resource.get();
}

auto SceneTextures::GetVelocityResource() const
  -> const std::shared_ptr<graphics::Texture>&
{
  return velocity_.resource;
}

auto SceneTextures::GetCustomDepth() const -> graphics::Texture*
{
  return custom_depth_.resource.get();
}

auto SceneTextures::GetCustomStencil() const -> SceneTextureAspectView
{
  return {
    .texture = custom_depth_.resource.get(),
    .aspect = SceneTextureAspectView::Aspect::kStencil,
  };
}

void SceneTextures::Resize(const glm::uvec2 new_extent)
{
  if (new_extent.x == config_.extent.x && new_extent.y == config_.extent.y) {
    return;
  }

  ValidateExtent(new_extent);
  config_.extent = new_extent;
  AllocateTextures();
}

void SceneTextures::RebuildWithGBuffers()
{
  // This helper only validates the Stage-9-written GBuffer family. The
  // SceneRenderer-owned Stage 10 boundary remains responsible for setup-mode
  // promotion, binding regeneration, and downstream publication.
  for (std::size_t i = 0; i < kActiveGBufferCount; ++i) {
    const auto index = static_cast<GBufferIndex>(i);
    static_cast<void>(RequireTexture(gbuffers_.at(i), GBufferName(index)));
  }
}

auto SceneTextures::GetExtent() const noexcept -> glm::uvec2
{
  return config_.extent;
}

auto SceneTextures::GetConfig() const noexcept -> const SceneTexturesConfig&
{
  return config_;
}

void SceneTextures::ValidateConfig(const SceneTexturesConfig& config)
{
  ValidateExtent(config.extent);
  if (config.gbuffer_count != kActiveGBufferCount) {
    throw std::invalid_argument(
      "SceneTextures requires exactly four active GBuffers in the current "
      "Vortex contract");
  }
}

void SceneTextures::ValidateExtent(const glm::uvec2 extent)
{
  if (extent.x == 0 || extent.y == 0) {
    throw std::invalid_argument(
      "SceneTextures: extent must be non-zero in both dimensions");
  }
}

void SceneTextures::AllocateTextures()
{
  ReleaseTextures();

  scene_color_.resource
    = CreateTexture("SceneColor", Format::kRGBA16Float, true, true, true);
  RegisterTexture(scene_color_);
  scene_depth_.resource
    = CreateTexture("SceneDepth", Format::kDepth32Stencil8, true, true);
  RegisterTexture(scene_depth_);
  partial_depth_.resource
    = CreateTexture("PartialDepth", Format::kR32Float, true, true);
  RegisterTexture(partial_depth_);

  gbuffers_.fill({});
  for (std::size_t i = 0; i < kActiveGBufferCount; ++i) {
    const auto index = static_cast<GBufferIndex>(i);
    gbuffers_.at(i).resource
      = CreateTexture(GBufferName(index), GBufferFormat(index), true, true);
    RegisterTexture(gbuffers_.at(i));
  }

  velocity_.resource = config_.enable_velocity
    ? CreateTexture("Velocity", Format::kRG16Float, true, true, true)
    : nullptr;
  if (velocity_.resource != nullptr) {
    RegisterTexture(velocity_);
  }

  custom_depth_.resource = config_.enable_custom_depth
    ? CreateTexture("CustomDepth", Format::kDepth32Stencil8, true, true)
    : nullptr;
  if (custom_depth_.resource != nullptr) {
    RegisterTexture(custom_depth_);
  }
}

void SceneTextures::ReleaseTextures() noexcept
{
  for (auto& gbuffer : gbuffers_) {
    UnregisterTexture(gbuffer);
  }
  UnregisterTexture(scene_color_);
  UnregisterTexture(scene_depth_);
  UnregisterTexture(partial_depth_);
  UnregisterTexture(velocity_);
  UnregisterTexture(custom_depth_);
}

void SceneTextures::RegisterTexture(RegisteredTexture& texture)
{
  if (texture.resource == nullptr) {
    return;
  }
  // Native resource handles can be reused by the backend after a resize. Drop
  // any queue-level state keyed by the same native handle before the new scene
  // texture participates in command recording; its descriptor initial_state is
  // the authoritative starting state for this allocation.
  gfx_.ForgetKnownResourceState(*texture.resource);
  gfx_.GetResourceRegistry().Register(texture.resource);
}

void SceneTextures::UnregisterTexture(RegisteredTexture& texture) noexcept
{
  auto resource = std::move(texture.resource);
  if (resource == nullptr) {
    return;
  }

  auto& registry = gfx_.GetResourceRegistry();
  gfx_.ForgetKnownResourceState(*resource);
  if (registry.Contains(*resource)) {
    registry.UnRegisterResource(*resource);
  }
  gfx_.RegisterDeferredRelease(std::move(resource));
}

auto SceneTextures::CreateTexture(std::string_view debug_name,
  const Format format, const bool shader_resource, const bool render_target,
  const bool uav) const -> std::shared_ptr<graphics::Texture>
{
  const auto& format_info = graphics::detail::GetFormatInfo(format);

  graphics::TextureDesc desc {};
  desc.width = config_.extent.x;
  desc.height = config_.extent.y;
  desc.depth = 1;
  desc.array_size = 1;
  desc.mip_levels = 1;
  desc.sample_count = config_.msaa_sample_count;
  desc.sample_quality = 0;
  desc.format = format;
  desc.texture_type = TextureType::kTexture2D;
  desc.debug_name = std::string(debug_name);
  desc.is_shader_resource = shader_resource;
  desc.is_render_target = render_target;
  desc.is_uav = uav;
  desc.is_typeless
    = shader_resource && (format_info.has_depth || format_info.has_stencil);
  if (render_target) {
    desc.initial_state = format_info.has_depth || format_info.has_stencil
      ? graphics::ResourceStates::kDepthWrite
      : graphics::ResourceStates::kRenderTarget;
    desc.use_clear_value = true;
    desc.clear_value = (format_info.has_depth || format_info.has_stencil)
      ? graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F }
      : graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  } else if (shader_resource) {
    desc.initial_state = graphics::ResourceStates::kShaderResource;
  }
  return gfx_.CreateTexture(desc);
}

auto SceneTextures::RequireTexture(const RegisteredTexture& texture,
  std::string_view name) const -> graphics::Texture&
{
  CHECK_F(texture.resource != nullptr,
    "SceneTextures: missing required texture {}", name);
  return *texture.resource;
}

} // namespace oxygen::vortex
