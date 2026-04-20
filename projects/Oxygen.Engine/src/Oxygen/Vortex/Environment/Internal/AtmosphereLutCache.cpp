//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>

#include <algorithm>
#include <bit>
#include <stdexcept>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex::environment::internal {

namespace {

  constexpr std::uint32_t kLutValidTransmittance = 1U << 0U;
  constexpr std::uint32_t kLutValidMultiScattering = 1U << 1U;
  constexpr std::uint32_t kLutValidDistantSkyLight = 1U << 2U;

  auto CombineHashU64(std::uint64_t seed, const std::uint64_t value)
    -> std::uint64_t
  {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
  }

  auto FloatBits(const float value) -> std::uint32_t
  {
    return std::bit_cast<std::uint32_t>(value);
  }

} // namespace

AtmosphereLutCache::AtmosphereLutCache(Renderer& renderer)
  : renderer_(renderer)
{
}

AtmosphereLutCache::~AtmosphereLutCache() { ResetResources(); }

auto AtmosphereLutCache::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
}

auto AtmosphereLutCache::RefreshForState(
  const StableAtmosphereState& stable_state) -> void
{
  const auto next_internal_parameters = BuildInternalParameters(stable_state);
  const auto internal_parameter_hash
    = HashInternalParameters(next_internal_parameters);
  const auto next_scattering_lut_family_revision
    = CombineHashU64(stable_state.atmosphere_revision, internal_parameter_hash);
  const auto next_distant_sky_light_lut_revision = CombineHashU64(
    next_scattering_lut_family_revision, stable_state.light_revision);

  state_.atmosphere_enabled = stable_state.view_products.atmosphere.enabled;
  state_.dual_light_participating
    = stable_state.view_products.atmosphere_light_count >= 2U;
  state_.atmosphere_revision = stable_state.atmosphere_revision;
  state_.atmosphere_light_revision = stable_state.light_revision;
  state_.internal_parameters = next_internal_parameters;

  if (!state_.atmosphere_enabled) {
    InvalidateScatteringLutFamily();
    InvalidateDistantSkyLightLut();
    state_.scattering_lut_family_revision = next_scattering_lut_family_revision;
    state_.distant_sky_light_lut_revision = next_distant_sky_light_lut_revision;
    return;
  }

  if (state_.scattering_lut_family_revision
    != next_scattering_lut_family_revision) {
    state_.scattering_lut_family_revision = next_scattering_lut_family_revision;
    InvalidateScatteringLutFamily();
    InvalidateDistantSkyLightLut();
  }

  if (state_.distant_sky_light_lut_revision
    != next_distant_sky_light_lut_revision) {
    state_.distant_sky_light_lut_revision = next_distant_sky_light_lut_revision;
    InvalidateDistantSkyLightLut();
  }

  static_cast<void>(EnsureResources());
}

auto AtmosphereLutCache::EnsureResources() -> bool
{
  if (!state_.atmosphere_enabled) {
    return false;
  }

  const auto params = state_.internal_parameters;
  const auto ready = EnsureTexture(transmittance_texture_, transmittance_slots_,
                       "Vortex.Environment.AtmosphereTransmittanceLut",
                       params.transmittance_width, params.transmittance_height)
    && EnsureTexture(multi_scattering_texture_, multi_scattering_slots_,
      "Vortex.Environment.AtmosphereMultiScatteringLut",
      params.multi_scattering_width, params.multi_scattering_height)
    && EnsureStructuredBuffer(distant_sky_light_buffer_,
      distant_sky_light_slots_, "Vortex.Environment.DistantSkyLightLut",
      sizeof(float) * 4U, sizeof(float) * 4U);
  if (!ready) {
    return false;
  }

  state_.transmittance_lut_srv = transmittance_slots_.srv;
  state_.transmittance_lut_uav = transmittance_slots_.uav;
  state_.multi_scattering_lut_srv = multi_scattering_slots_.srv;
  state_.multi_scattering_lut_uav = multi_scattering_slots_.uav;
  state_.distant_sky_light_lut_srv = distant_sky_light_slots_.srv;
  state_.distant_sky_light_lut_uav = distant_sky_light_slots_.uav;
  return true;
}

auto AtmosphereLutCache::MarkTransmittanceValid() -> void
{
  state_.transmittance_lut_valid = true;
  state_.valid_lut_mask |= kLutValidTransmittance;
  state_.cache_revision += 1U;
}

auto AtmosphereLutCache::MarkMultiScatteringValid() -> void
{
  state_.multi_scattering_lut_valid = true;
  state_.valid_lut_mask |= kLutValidMultiScattering;
  state_.cache_revision += 1U;
}

auto AtmosphereLutCache::MarkDistantSkyLightValid() -> void
{
  state_.distant_sky_light_lut_valid = true;
  state_.valid_lut_mask |= kLutValidDistantSkyLight;
  state_.cache_revision += 1U;
}

auto AtmosphereLutCache::ResetResources() -> void
{
  auto gfx = renderer_.GetGraphics();
  if (gfx != nullptr) {
    auto& registry = gfx->GetResourceRegistry();
    if (transmittance_texture_ != nullptr
      && registry.Contains(*transmittance_texture_)) {
      registry.UnRegisterResource(*transmittance_texture_);
    }
    if (multi_scattering_texture_ != nullptr
      && registry.Contains(*multi_scattering_texture_)) {
      registry.UnRegisterResource(*multi_scattering_texture_);
    }
    if (distant_sky_light_buffer_ != nullptr
      && registry.Contains(*distant_sky_light_buffer_)) {
      registry.UnRegisterResource(*distant_sky_light_buffer_);
    }
  }

  transmittance_texture_.reset();
  multi_scattering_texture_.reset();
  distant_sky_light_buffer_.reset();
  transmittance_slots_ = {};
  multi_scattering_slots_ = {};
  distant_sky_light_slots_ = {};
  state_.transmittance_lut_srv = kInvalidShaderVisibleIndex;
  state_.transmittance_lut_uav = kInvalidShaderVisibleIndex;
  state_.multi_scattering_lut_srv = kInvalidShaderVisibleIndex;
  state_.multi_scattering_lut_uav = kInvalidShaderVisibleIndex;
  state_.distant_sky_light_lut_srv = kInvalidShaderVisibleIndex;
  state_.distant_sky_light_lut_uav = kInvalidShaderVisibleIndex;
  InvalidateScatteringLutFamily();
  InvalidateDistantSkyLightLut();
}

auto AtmosphereLutCache::InvalidateScatteringLutFamily() -> void
{
  state_.transmittance_lut_valid = false;
  state_.multi_scattering_lut_valid = false;
  state_.valid_lut_mask &= ~kLutValidTransmittance;
  state_.valid_lut_mask &= ~kLutValidMultiScattering;
}

auto AtmosphereLutCache::InvalidateDistantSkyLightLut() -> void
{
  state_.distant_sky_light_lut_valid = false;
  state_.valid_lut_mask &= ~kLutValidDistantSkyLight;
}

auto AtmosphereLutCache::EnsureTexture(
  std::shared_ptr<graphics::Texture>& texture, TextureSlots& slots,
  const std::string_view debug_name, const std::uint32_t width,
  const std::uint32_t height) -> bool
{
  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  const auto wants_recreate = texture == nullptr
    || texture->GetDescriptor().width != width
    || texture->GetDescriptor().height != height
    || texture->GetDescriptor().format != Format::kRGBA16Float;

  auto& registry = gfx->GetResourceRegistry();
  if (wants_recreate) {
    if (texture != nullptr && registry.Contains(*texture)) {
      registry.UnRegisterResource(*texture);
    }
    texture = gfx->CreateTexture({
      .width = width,
      .height = height,
      .depth = 1U,
      .array_size = 1U,
      .mip_levels = 1U,
      .sample_count = 1U,
      .sample_quality = 0U,
      .format = Format::kRGBA16Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = std::string(debug_name),
      .is_shader_resource = true,
      .is_render_target = false,
      .is_uav = true,
      .is_typeless = false,
      .is_shading_rate_surface = false,
      .clear_value = {},
      .use_clear_value = false,
      .initial_state = graphics::ResourceStates::kCommon,
      .cpu_access = graphics::ResourceAccessMode::kImmutable,
    });
    if (texture == nullptr) {
      return false;
    }
    texture->SetName(debug_name);
    registry.Register(texture);
    slots = {};
  } else if (!registry.Contains(*texture)) {
    registry.Register(texture);
  }

  if (!slots.srv.IsValid()) {
    auto handle = gfx->GetDescriptorAllocator().AllocateRaw(
      graphics::ResourceViewType::kTexture_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid()) {
      throw std::runtime_error(
        "AtmosphereLutCache: failed to allocate texture SRV");
    }
    slots.srv = gfx->GetDescriptorAllocator().GetShaderVisibleIndex(handle);
    registry.RegisterView(*texture, std::move(handle),
      graphics::TextureViewDescription {
        .view_type = graphics::ResourceViewType::kTexture_SRV,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .format = Format::kRGBA16Float,
        .dimension = TextureType::kTexture2D,
        .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
        .is_read_only_dsv = false,
      });
  }

  if (!slots.uav.IsValid()) {
    auto handle = gfx->GetDescriptorAllocator().AllocateRaw(
      graphics::ResourceViewType::kTexture_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid()) {
      throw std::runtime_error(
        "AtmosphereLutCache: failed to allocate texture UAV");
    }
    slots.uav = gfx->GetDescriptorAllocator().GetShaderVisibleIndex(handle);
    registry.RegisterView(*texture, std::move(handle),
      graphics::TextureViewDescription {
        .view_type = graphics::ResourceViewType::kTexture_UAV,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .format = Format::kRGBA16Float,
        .dimension = TextureType::kTexture2D,
        .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
        .is_read_only_dsv = false,
      });
  }

  return true;
}

auto AtmosphereLutCache::EnsureStructuredBuffer(
  std::shared_ptr<graphics::Buffer>& buffer, BufferSlots& slots,
  const std::string_view debug_name, const std::uint64_t size_bytes,
  const std::uint32_t stride) -> bool
{
  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  const auto wants_recreate
    = buffer == nullptr || buffer->GetSize() != size_bytes;
  auto& registry = gfx->GetResourceRegistry();
  if (wants_recreate) {
    if (buffer != nullptr && registry.Contains(*buffer)) {
      registry.UnRegisterResource(*buffer);
    }
    buffer = gfx->CreateBuffer({
      .size_bytes = size_bytes,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    if (buffer == nullptr) {
      return false;
    }
    buffer->SetName(debug_name);
    registry.Register(buffer);
    slots = {};
  } else if (!registry.Contains(*buffer)) {
    registry.Register(buffer);
  }

  if (!slots.srv.IsValid()) {
    auto handle = gfx->GetDescriptorAllocator().AllocateRaw(
      graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid()) {
      throw std::runtime_error(
        "AtmosphereLutCache: failed to allocate buffer SRV");
    }
    slots.srv = gfx->GetDescriptorAllocator().GetShaderVisibleIndex(handle);
    registry.RegisterView(*buffer, std::move(handle),
      graphics::BufferViewDescription {
        .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .format = Format::kUnknown,
        .range = {},
        .stride = stride,
      });
  }

  if (!slots.uav.IsValid()) {
    auto handle = gfx->GetDescriptorAllocator().AllocateRaw(
      graphics::ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid()) {
      throw std::runtime_error(
        "AtmosphereLutCache: failed to allocate buffer UAV");
    }
    slots.uav = gfx->GetDescriptorAllocator().GetShaderVisibleIndex(handle);
    registry.RegisterView(*buffer, std::move(handle),
      graphics::BufferViewDescription {
        .view_type = graphics::ResourceViewType::kStructuredBuffer_UAV,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .format = Format::kUnknown,
        .range = {},
        .stride = stride,
      });
  }

  return true;
}

auto AtmosphereLutCache::BuildInternalParameters(
  const StableAtmosphereState& stable_state) -> InternalParameters
{
  const auto& atmosphere = stable_state.view_products.atmosphere;
  auto params = InternalParameters {};
  params.sky_luminance_factor_rgb = atmosphere.sky_luminance_factor_rgb;
  params.sky_and_aerial_perspective_luminance_factor_rgb
    = atmosphere.sky_and_aerial_perspective_luminance_factor_rgb;
  params.aerial_perspective_distance_scale
    = atmosphere.aerial_perspective_distance_scale;
  params.aerial_scattering_strength = atmosphere.aerial_scattering_strength;
  params.transmittance_sample_count
    = std::clamp(10.0F * atmosphere.trace_sample_count_scale, 1.0F, 64.0F);
  params.multi_scattering_sample_count
    = std::clamp(15.0F * atmosphere.trace_sample_count_scale, 1.0F, 64.0F);
  params.sky_view_sample_count_min
    = std::clamp(4.0F * atmosphere.trace_sample_count_scale, 1.0F, 64.0F);
  params.sky_view_sample_count_max
    = std::clamp(32.0F * atmosphere.trace_sample_count_scale,
      params.sky_view_sample_count_min + 1.0F, 128.0F);
  params.camera_aerial_sample_count_per_slice
    = std::clamp(2.0F * atmosphere.trace_sample_count_scale, 1.0F, 8.0F);
  params.camera_aerial_depth_slice_length_km = params.camera_aerial_depth_km
    / static_cast<float>(std::max(params.camera_aerial_depth_resolution, 1U));
  return params;
}

auto AtmosphereLutCache::HashInternalParameters(
  const InternalParameters& params) -> std::uint64_t
{
  auto seed = std::uint64_t { 0U };
  seed = CombineHashU64(seed, params.transmittance_width);
  seed = CombineHashU64(seed, params.transmittance_height);
  seed = CombineHashU64(seed, params.multi_scattering_width);
  seed = CombineHashU64(seed, params.multi_scattering_height);
  seed = CombineHashU64(seed, params.sky_view_width);
  seed = CombineHashU64(seed, params.sky_view_height);
  seed = CombineHashU64(seed, FloatBits(params.sky_view_sample_count_min));
  seed = CombineHashU64(seed, FloatBits(params.sky_view_sample_count_max));
  seed = CombineHashU64(
    seed, FloatBits(params.sky_view_distance_to_sample_count_max_m));
  seed = CombineHashU64(seed, params.camera_aerial_width);
  seed = CombineHashU64(seed, params.camera_aerial_height);
  seed = CombineHashU64(seed, params.camera_aerial_depth_resolution);
  seed = CombineHashU64(seed, FloatBits(params.camera_aerial_depth_km));
  seed = CombineHashU64(
    seed, FloatBits(params.camera_aerial_depth_slice_length_km));
  seed = CombineHashU64(
    seed, FloatBits(params.camera_aerial_sample_count_per_slice));
  seed = CombineHashU64(seed, FloatBits(params.transmittance_sample_count));
  seed = CombineHashU64(seed, FloatBits(params.multi_scattering_sample_count));
  seed = CombineHashU64(
    seed, FloatBits(params.distant_sky_light_sample_altitude_km));
  seed = CombineHashU64(seed, FloatBits(params.sky_luminance_factor_rgb.x));
  seed = CombineHashU64(seed, FloatBits(params.sky_luminance_factor_rgb.y));
  seed = CombineHashU64(seed, FloatBits(params.sky_luminance_factor_rgb.z));
  seed = CombineHashU64(
    seed, FloatBits(params.sky_and_aerial_perspective_luminance_factor_rgb.x));
  seed = CombineHashU64(
    seed, FloatBits(params.sky_and_aerial_perspective_luminance_factor_rgb.y));
  seed = CombineHashU64(
    seed, FloatBits(params.sky_and_aerial_perspective_luminance_factor_rgb.z));
  seed
    = CombineHashU64(seed, FloatBits(params.aerial_perspective_distance_scale));
  seed = CombineHashU64(seed, FloatBits(params.aerial_scattering_strength));
  return seed;
}

} // namespace oxygen::vortex::environment::internal
