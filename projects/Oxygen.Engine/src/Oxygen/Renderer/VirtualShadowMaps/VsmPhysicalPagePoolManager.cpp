//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>

#include <bit>
#include <stdexcept>
#include <string>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPoolCompatibility.h>

namespace oxygen::renderer::vsm {

namespace {

  constexpr auto kPhysicalPageMetadataStrideBytes = sizeof(std::uint32_t);

  auto BuildShadowPoolTextureDebugName(const VsmPhysicalPoolConfig& config)
    -> std::string
  {
    if (config.debug_name.empty()) {
      return "VsmShadowPhysicalPool";
    }

    return config.debug_name + ".ShadowPool";
  }

  auto BuildHzbPoolTextureDebugName(const VsmHzbPoolConfig& config)
    -> std::string
  {
    if (config.debug_name.empty()) {
      return "VsmHzbPool";
    }

    return config.debug_name + ".HzbPool";
  }

  auto BuildMetadataBufferDebugName(const VsmPhysicalPoolConfig& config)
    -> std::string
  {
    if (config.debug_name.empty()) {
      return "VsmPhysicalPageMetadata";
    }

    return config.debug_name + ".Metadata";
  }

  auto CreateShadowTexture(Graphics& gfx, const VsmPhysicalPoolConfig& config,
    const std::uint32_t tiles_per_axis) -> std::shared_ptr<graphics::Texture>
  {
    if (tiles_per_axis == 0) {
      throw std::runtime_error(
        "VsmPhysicalPagePoolManager: cannot create shadow pool texture "
        "for a configuration without a square per-slice page layout");
    }

    const auto texture_extent = config.page_size_texels * tiles_per_axis;
    auto desc = graphics::TextureDesc {};
    desc.width = texture_extent;
    desc.height = texture_extent;
    desc.array_size = config.array_slice_count;
    desc.mip_levels = 1;
    desc.format = config.depth_format;
    desc.texture_type = TextureType::kTexture2DArray;
    desc.debug_name = BuildShadowPoolTextureDebugName(config);
    desc.is_shader_resource = true;
    desc.is_render_target = true;
    desc.is_typeless = true;
    desc.use_clear_value = true;
    desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
    desc.initial_state = graphics::ResourceStates::kCommon;

    auto texture = gfx.CreateTexture(desc);
    if (!texture) {
      throw std::runtime_error(
        "VsmPhysicalPagePoolManager: failed to create shadow pool texture");
    }

    return texture;
  }

  auto CreateMetadataBuffer(Graphics& gfx, const VsmPhysicalPoolConfig& config)
    -> std::shared_ptr<graphics::Buffer>
  {
    auto desc = graphics::BufferDesc {};
    desc.size_bytes = static_cast<std::uint64_t>(config.physical_tile_capacity)
      * kPhysicalPageMetadataStrideBytes;
    desc.usage = graphics::BufferUsage::kStorage;
    desc.memory = graphics::BufferMemory::kDeviceLocal;
    desc.debug_name = BuildMetadataBufferDebugName(config);

    auto buffer = gfx.CreateBuffer(desc);
    if (!buffer) {
      throw std::runtime_error("VsmPhysicalPagePoolManager: failed to create "
                               "physical metadata buffer");
    }

    return buffer;
  }

  auto ComputeDerivedHzbWidth(const VsmPhysicalPoolConfig& config,
    const std::uint32_t tiles_per_axis) -> std::uint32_t
  {
    const auto shadow_extent = config.page_size_texels * tiles_per_axis;
    const auto rounded_extent = std::bit_ceil(shadow_extent);
    return rounded_extent > 1U ? rounded_extent >> 1U : 1U;
  }

  auto ComputeDerivedHzbHeight(const VsmPhysicalPoolConfig& config,
    const std::uint32_t tiles_per_axis) -> std::uint32_t
  {
    return ComputeDerivedHzbWidth(config, tiles_per_axis);
  }

  auto ComputeDerivedHzbArraySize() -> std::uint32_t { return 1U; }

  auto CreateHzbTexture(Graphics& gfx, const VsmHzbPoolConfig& config,
    const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t array_size) -> std::shared_ptr<graphics::Texture>
  {
    auto desc = graphics::TextureDesc {};
    desc.width = width;
    desc.height = height;
    desc.array_size = array_size;
    desc.mip_levels = config.mip_count;
    desc.format = config.format;
    desc.texture_type
      = array_size > 1 ? TextureType::kTexture2DArray : TextureType::kTexture2D;
    desc.debug_name = BuildHzbPoolTextureDebugName(config);
    desc.is_shader_resource = true;
    desc.is_uav = true;
    desc.initial_state = graphics::ResourceStates::kCommon;

    auto texture = gfx.CreateTexture(desc);
    if (!texture) {
      throw std::runtime_error(
        "VsmPhysicalPagePoolManager: failed to create HZB pool texture");
    }

    return texture;
  }

} // namespace

VsmPhysicalPagePoolManager::VsmPhysicalPagePoolManager(Graphics* gfx) noexcept
  : gfx_(gfx)
{
}

VsmPhysicalPagePoolManager::~VsmPhysicalPagePoolManager() = default;

auto VsmPhysicalPagePoolManager::EnsureShadowPool(
  const VsmPhysicalPoolConfig& config) -> VsmPhysicalPoolChangeResult
{
  if (!IsValid(config)) {
    const auto validation = Validate(config);
    LOG_F(WARNING,
      "rejecting invalid shadow pool config (reason={}, debug_name=`{}`)",
      to_string(validation), config.debug_name);
    throw std::invalid_argument(
      "VsmPhysicalPagePoolManager: invalid shadow pool config");
  }

  const auto compatibility = ComputeCompatibility(config);
  const auto tiles_per_axis = ComputeTilesPerAxis(
    config.physical_tile_capacity, config.array_slice_count);
  auto next_resources = VsmPhysicalPoolResources {};

  if (gfx_ != nullptr
    && (!shadow_state_.is_available
      || compatibility != VsmPhysicalPoolCompatibilityResult::kCompatible)) {
    next_resources.shadow_texture
      = CreateShadowTexture(*gfx_, config, tiles_per_axis);
    next_resources.metadata_buffer = CreateMetadataBuffer(*gfx_, config);
  }

  if (!shadow_state_.is_available) {
    shadow_state_.config = config;
    shadow_state_.tiles_per_axis = tiles_per_axis;
    shadow_state_.is_available = true;
    shadow_state_.pool_identity = next_pool_identity_++;
    shadow_resources_ = std::move(next_resources);
    DLOG_F(2,
      "created shadow pool id={} page_size={} tile_capacity={} slices={} "
      "tiles_per_axis={}",
      shadow_state_.pool_identity, shadow_state_.config.page_size_texels,
      shadow_state_.config.physical_tile_capacity,
      shadow_state_.config.array_slice_count, shadow_state_.tiles_per_axis);
    return VsmPhysicalPoolChangeResult::kCreated;
  }

  if (compatibility == VsmPhysicalPoolCompatibilityResult::kCompatible) {
    DLOG_F(
      3, "shadow pool config unchanged for id={}", shadow_state_.pool_identity);
    return VsmPhysicalPoolChangeResult::kUnchanged;
  }

  DLOG_F(2, "recreating shadow pool id={} due to {}",
    shadow_state_.pool_identity, to_string(compatibility));
  shadow_state_.config = config;
  shadow_state_.tiles_per_axis = tiles_per_axis;
  shadow_state_.is_available = true;
  shadow_state_.pool_identity = next_pool_identity_++;
  shadow_resources_ = std::move(next_resources);
  if (hzb_state_.is_available) {
    DLOG_F(2, "invalidating derived HZB pool because shadow pool changed");
    hzb_state_ = {};
    hzb_resources_ = {};
  }
  return VsmPhysicalPoolChangeResult::kRecreated;
}

auto VsmPhysicalPagePoolManager::EnsureHzbPool(const VsmHzbPoolConfig& config)
  -> VsmHzbPoolChangeResult
{
  if (!IsValid(config)) {
    const auto validation = Validate(config);
    LOG_F(WARNING,
      "rejecting invalid HZB pool config (reason={}, debug_name=`{}`)",
      to_string(validation), config.debug_name);
    throw std::invalid_argument(
      "VsmPhysicalPagePoolManager: invalid HZB pool config");
  }

  if (!shadow_state_.is_available) {
    LOG_F(
      WARNING, "rejecting HZB pool config because no shadow pool is active");
    throw std::logic_error(
      "VsmPhysicalPagePoolManager: HZB pool requires an active shadow pool");
  }

  const auto previous_config = hzb_state_.config;
  const auto previous_width = hzb_state_.width;
  const auto previous_height = hzb_state_.height;
  const auto previous_array_size = hzb_state_.array_size;
  const auto was_available = hzb_state_.is_available;
  const auto derived_width = ComputeDerivedHzbWidth(
    shadow_state_.config, shadow_state_.tiles_per_axis);
  const auto derived_height = ComputeDerivedHzbHeight(
    shadow_state_.config, shadow_state_.tiles_per_axis);
  const auto derived_array_size = ComputeDerivedHzbArraySize();
  auto next_resources = hzb_resources_;

  if (gfx_ != nullptr
    && (!was_available || previous_config != config
      || previous_width != derived_width || previous_height != derived_height
      || previous_array_size != derived_array_size)) {
    next_resources.texture = CreateHzbTexture(
      *gfx_, config, derived_width, derived_height, derived_array_size);
  }

  hzb_state_.config = config;
  hzb_state_.is_available = true;
  hzb_state_.width = derived_width;
  hzb_state_.height = derived_height;
  hzb_state_.array_size = derived_array_size;
  hzb_resources_ = std::move(next_resources);

  if (!was_available) {
    DLOG_F(2, "created HZB pool {}x{} mips={} array_size={}", hzb_state_.width,
      hzb_state_.height, hzb_state_.config.mip_count, hzb_state_.array_size);
    return VsmHzbPoolChangeResult::kCreated;
  }

  if (previous_config == config && previous_width == derived_width
    && previous_height == derived_height
    && previous_array_size == derived_array_size) {
    DLOG_F(3, "HZB pool config unchanged");
    return VsmHzbPoolChangeResult::kUnchanged;
  }

  DLOG_F(2, "recreated HZB pool {}x{} mips={} array_size={}", hzb_state_.width,
    hzb_state_.height, hzb_state_.config.mip_count, hzb_state_.array_size);
  return VsmHzbPoolChangeResult::kRecreated;
}

auto VsmPhysicalPagePoolManager::Reset() -> void
{
  DLOG_F(2, "reset shadow_available={} hzb_available={} pool_id={}",
    shadow_state_.is_available, hzb_state_.is_available,
    shadow_state_.pool_identity);
  shadow_state_ = {};
  hzb_state_ = {};
  shadow_resources_ = {};
  hzb_resources_ = {};
}

auto VsmPhysicalPagePoolManager::IsShadowPoolAvailable() const noexcept -> bool
{
  return shadow_state_.is_available;
}

auto VsmPhysicalPagePoolManager::IsHzbPoolAvailable() const noexcept -> bool
{
  return hzb_state_.is_available;
}

auto VsmPhysicalPagePoolManager::GetPoolIdentity() const noexcept
  -> std::uint64_t
{
  return shadow_state_.pool_identity;
}

auto VsmPhysicalPagePoolManager::GetSliceCount() const noexcept -> std::uint32_t
{
  return shadow_state_.config.array_slice_count;
}

auto VsmPhysicalPagePoolManager::GetSliceRoles() const noexcept
  -> std::span<const VsmPhysicalPoolSliceRole>
{
  return { shadow_state_.config.slice_roles.data(),
    shadow_state_.config.slice_roles.size() };
}

auto VsmPhysicalPagePoolManager::GetTileCapacity() const noexcept
  -> std::uint32_t
{
  return shadow_state_.config.physical_tile_capacity;
}

auto VsmPhysicalPagePoolManager::GetTilesPerAxis() const noexcept
  -> std::uint32_t
{
  return shadow_state_.tiles_per_axis;
}

auto VsmPhysicalPagePoolManager::GetShadowPoolSnapshot() const noexcept
  -> VsmPhysicalPoolSnapshot
{
  return VsmPhysicalPoolSnapshot {
    .pool_identity = shadow_state_.pool_identity,
    .page_size_texels = shadow_state_.config.page_size_texels,
    .tile_capacity = shadow_state_.config.physical_tile_capacity,
    .tiles_per_axis = shadow_state_.tiles_per_axis,
    .slice_count = shadow_state_.config.array_slice_count,
    .depth_format = shadow_state_.config.depth_format,
    .slice_roles = shadow_state_.config.slice_roles,
    .shadow_texture = shadow_resources_.shadow_texture,
    .metadata_buffer = shadow_resources_.metadata_buffer,
    .is_available = shadow_state_.is_available,
  };
}

auto VsmPhysicalPagePoolManager::GetHzbPoolSnapshot() const noexcept
  -> VsmHzbPoolSnapshot
{
  return VsmHzbPoolSnapshot {
    .width = hzb_state_.width,
    .height = hzb_state_.height,
    .mip_count = hzb_state_.config.mip_count,
    .array_size = hzb_state_.array_size,
    .format = hzb_state_.config.format,
    .texture = hzb_resources_.texture,
    .is_available = hzb_state_.is_available,
  };
}

auto VsmPhysicalPagePoolManager::GetMetadataBuffer() const noexcept
  -> std::shared_ptr<const graphics::Buffer>
{
  return shadow_resources_.metadata_buffer;
}

auto VsmPhysicalPagePoolManager::IsCompatible(
  const VsmPhysicalPoolConfig& config) const noexcept -> bool
{
  return ComputeCompatibility(config)
    == VsmPhysicalPoolCompatibilityResult::kCompatible;
}

auto VsmPhysicalPagePoolManager::ComputeCompatibility(
  const VsmPhysicalPoolConfig& config) const noexcept
  -> VsmPhysicalPoolCompatibilityResult
{
  if (!IsValid(config)) {
    return VsmPhysicalPoolCompatibilityResult::kInvalidRequestedConfig;
  }

  if (!shadow_state_.is_available) {
    return VsmPhysicalPoolCompatibilityResult::kUnavailable;
  }

  return ComputePhysicalPoolCompatibility(shadow_state_.config, config);
}

} // namespace oxygen::renderer::vsm
