//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Nexus/FrameDrivenIndexReuse.h>
#include <Oxygen/Renderer/Resources/IResourceBinder.h>
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/ScenePrep/MaterialRef.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Upload/AtlasBuffer.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

using oxygen::engine::upload::AtlasBuffer;

namespace {

//! Validate material asset for common issues and constraints.
[[nodiscard]] auto ValidateMaterial(
  const oxygen::data::MaterialAsset& material, std::string& error_msg) -> bool
{
  // Check for reasonable scalar values
  constexpr float kReasonableScaleMax = 10.0F;

  const auto base_color = material.GetBaseColor();
  for (int i = 0; i < 4; ++i) {
    // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
    if (!std::isfinite(base_color[i])) {
      error_msg = "Material base_color contains non-finite values";
      return false;
    }
    if (base_color[i] < 0.0F || base_color[i] > kReasonableScaleMax) {
      error_msg = "Material base_color values out of reasonable range [0, 10]";
      return false;
    }
    // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)
  }

  const auto metalness = material.GetMetalness();
  if (!std::isfinite(metalness) || metalness < 0.0F || metalness > 1.0F) {
    error_msg = "Material metalness out of valid range [0, 1]";
    return false;
  }

  const auto roughness = material.GetRoughness();
  if (!std::isfinite(roughness) || roughness < 0.0F || roughness > 1.0F) {
    error_msg = "Material roughness out of valid range [0, 1]";
    return false;
  }

  const auto normal_scale = material.GetNormalScale();
  if (!std::isfinite(normal_scale) || normal_scale < 0.0F
    || normal_scale > kReasonableScaleMax) {
    error_msg = "Material normal_scale out of reasonable range [0, 10]";
    return false;
  }

  const auto ambient_occlusion = material.GetAmbientOcclusion();
  if (!std::isfinite(ambient_occlusion) || ambient_occlusion < 0.0F
    || ambient_occlusion > 1.0F) {
    error_msg = "Material ambient_occlusion out of valid range [0, 1]";
    return false;
  }

  if (material.HasProceduralGrid()) {
    const auto grid_spacing = material.GetGridSpacing();
    if (!std::isfinite(grid_spacing[0]) || !std::isfinite(grid_spacing[1])
      || std::abs(grid_spacing[0]) <= oxygen::math::Epsilon
      || std::abs(grid_spacing[1]) <= oxygen::math::Epsilon) {
      error_msg = "Material grid_spacing must be finite and non-zero";
      return false;
    }
  }

  return true;
}

//! Create a content-based hash key for material deduplication.
auto MakeMaterialKey(
  const oxygen::engine::sceneprep::MaterialRef& material) noexcept
  -> std::uint64_t
{
  // Hash based on stable, renderer-facing identity only:
  // - Material scalar/vector properties
  // - Texture ResourceKeys (opaque identifiers)
  // - Domain/flags
  // Avoid hashing raw author indices to prevent identity leaks and improve
  // stability across pipelines.

  std::size_t seed = 0U;
  constexpr float kScalarScale = 1024.0F;

  const auto base_color = material.resolved_asset->GetBaseColor();
  for (int i = 0; i < 4; ++i) {
    constexpr float kColorScale = 1024.0F;
    const auto quantized
      // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
      = static_cast<std::uint32_t>(std::round(base_color[i] * kColorScale));
    oxygen::HashCombine(seed, quantized);
  }

  const auto metalness_q = static_cast<std::uint32_t>(
    std::round(material.resolved_asset->GetMetalness() * kScalarScale));
  const auto roughness_q = static_cast<std::uint32_t>(
    std::round(material.resolved_asset->GetRoughness() * kScalarScale));
  const auto normal_scale_q = static_cast<std::uint32_t>(
    std::round(material.resolved_asset->GetNormalScale() * kScalarScale));
  const auto ao_q = static_cast<std::uint32_t>(
    std::round(material.resolved_asset->GetAmbientOcclusion() * kScalarScale));

  oxygen::HashCombine(seed, metalness_q);
  oxygen::HashCombine(seed, roughness_q);
  oxygen::HashCombine(seed, normal_scale_q);
  oxygen::HashCombine(seed, ao_q);

  oxygen::HashCombine(seed, material.resolved_asset->GetBaseColorTextureKey());
  oxygen::HashCombine(seed, material.resolved_asset->GetNormalTextureKey());
  oxygen::HashCombine(seed, material.resolved_asset->GetMetallicTextureKey());
  oxygen::HashCombine(seed, material.resolved_asset->GetRoughnessTextureKey());
  oxygen::HashCombine(
    seed, material.resolved_asset->GetAmbientOcclusionTextureKey());
  oxygen::HashCombine(seed, material.resolved_asset->GetEmissiveTextureKey());
  oxygen::HashCombine(seed, material.resolved_asset->GetSpecularTextureKey());
  oxygen::HashCombine(seed, material.resolved_asset->GetSheenColorTextureKey());
  oxygen::HashCombine(seed, material.resolved_asset->GetClearcoatTextureKey());
  oxygen::HashCombine(
    seed, material.resolved_asset->GetClearcoatNormalTextureKey());
  oxygen::HashCombine(
    seed, material.resolved_asset->GetTransmissionTextureKey());
  oxygen::HashCombine(seed, material.resolved_asset->GetThicknessTextureKey());

  const auto material_domain = material.resolved_asset->GetMaterialDomain();
  const auto material_flags = material.resolved_asset->GetFlags();
  oxygen::HashCombine(seed, material_domain);
  oxygen::HashCombine(seed, material_flags);
  if (material.resolved_asset->HasProceduralGrid()) {
    const auto grid_spacing = material.resolved_asset->GetGridSpacing();
    oxygen::HashCombine(seed, grid_spacing[0]);
    oxygen::HashCombine(seed, grid_spacing[1]);
    oxygen::HashCombine(seed, material.resolved_asset->GetGridMajorEvery());
    oxygen::HashCombine(seed, material.resolved_asset->GetGridLineThickness());
    oxygen::HashCombine(seed, material.resolved_asset->GetGridMajorThickness());
    oxygen::HashCombine(seed, material.resolved_asset->GetGridAxisThickness());
    oxygen::HashCombine(seed, material.resolved_asset->GetGridFadeStart());
    oxygen::HashCombine(seed, material.resolved_asset->GetGridFadeEnd());
    {
      const auto grid_minor = material.resolved_asset->GetGridMinorColor();
      oxygen::HashCombine(seed, grid_minor[0]);
      oxygen::HashCombine(seed, grid_minor[1]);
      oxygen::HashCombine(seed, grid_minor[2]);
      oxygen::HashCombine(seed, grid_minor[3]);
    }
    {
      const auto grid_major = material.resolved_asset->GetGridMajorColor();
      oxygen::HashCombine(seed, grid_major[0]);
      oxygen::HashCombine(seed, grid_major[1]);
      oxygen::HashCombine(seed, grid_major[2]);
      oxygen::HashCombine(seed, grid_major[3]);
    }
    {
      const auto grid_axis_x = material.resolved_asset->GetGridAxisColorX();
      oxygen::HashCombine(seed, grid_axis_x[0]);
      oxygen::HashCombine(seed, grid_axis_x[1]);
      oxygen::HashCombine(seed, grid_axis_x[2]);
      oxygen::HashCombine(seed, grid_axis_x[3]);
    }
    {
      const auto grid_axis_y = material.resolved_asset->GetGridAxisColorY();
      oxygen::HashCombine(seed, grid_axis_y[0]);
      oxygen::HashCombine(seed, grid_axis_y[1]);
      oxygen::HashCombine(seed, grid_axis_y[2]);
      oxygen::HashCombine(seed, grid_axis_y[3]);
    }
    {
      const auto grid_origin = material.resolved_asset->GetGridOriginColor();
      oxygen::HashCombine(seed, grid_origin[0]);
      oxygen::HashCombine(seed, grid_origin[1]);
      oxygen::HashCombine(seed, grid_origin[2]);
      oxygen::HashCombine(seed, grid_origin[3]);
    }
  }

  return static_cast<std::uint64_t>(seed);
}

//! Serialize MaterialAsset data into MaterialConstants format.
auto SerializeMaterialConstants(
  const oxygen::engine::sceneprep::MaterialRef& material,
  oxygen::renderer::resources::IResourceBinder& texture_binder) noexcept
  -> oxygen::engine::MaterialConstants
{
  oxygen::engine::MaterialConstants constants;

  // Copy base color
  const auto base_color = material.resolved_asset->GetBaseColor();
  constants.base_color
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    = { base_color[0], base_color[1], base_color[2], base_color[3] };

  // Copy scalar properties
  constants.metalness = material.resolved_asset->GetMetalness();
  constants.roughness = material.resolved_asset->GetRoughness();
  constants.normal_scale = material.resolved_asset->GetNormalScale();
  constants.ambient_occlusion = material.resolved_asset->GetAmbientOcclusion();

  // Resolve texture resource keys to bindless SRV indices.
  //
  // Important semantics (must match shader code):
  // - `oxygen::kInvalidShaderVisibleIndex` means "do not sample"
  //   (use scalar fallback only).
  // - Valid indices (including 0) are sampled from the bindless heap.
  //
  // Contract with PAK format:
  // - Texture author indices are `0` for the fallback texture.
  // - "No texture (skip sampling)" is encoded via the material flag
  //   `kMaterialFlag_NoTextureSampling`.
  const auto no_texture_sampling
    = (material.resolved_asset->GetFlags()
        & oxygen::data::pak::kMaterialFlag_NoTextureSampling)
    != 0U;

  const auto kDoNotSample = oxygen::kInvalidShaderVisibleIndex;

  const auto ResolveTextureIndex
    = [&texture_binder, no_texture_sampling](
        const oxygen::content::ResourceKey key,
        const std::uint32_t authored_index) -> oxygen::ShaderVisibleIndex {
    if (no_texture_sampling) {
      return oxygen::kInvalidShaderVisibleIndex;
    }

    if (key.get() != 0U) {
      return texture_binder.GetOrAllocate(key);
    }

    // No runtime key:
    // - If author index is 0, the material requests the fallback texture.
    // - If author index is non-zero, a texture was authored but not resolved
    //   yet, so bind a shared placeholder to keep sampling stable.
    if (authored_index == oxygen::data::pak::kFallbackResourceIndex) {
      return texture_binder.GetOrAllocate(
        oxygen::content::ResourceKey::kFallback);
    }
    return texture_binder.GetOrAllocate(
      oxygen::content::ResourceKey::kPlaceholder);
  };

  // For normal/ORM slots there is no "fallback texture". If the texture is
  // missing (including authored fallback index), do not sample and rely on
  // scalar defaults in the shader.
  const auto ResolveOptionalTextureIndex
    = [kDoNotSample, &texture_binder, no_texture_sampling](
        const oxygen::content::ResourceKey key) -> oxygen::ShaderVisibleIndex {
    if (no_texture_sampling) {
      return kDoNotSample;
    }
    if (key.get() == 0U) {
      return kDoNotSample;
    }
    return texture_binder.GetOrAllocate(key);
  };

  constants.base_color_texture_index
    = ResolveTextureIndex(material.resolved_asset->GetBaseColorTextureKey(),
      material.resolved_asset->GetBaseColorTexture());
  constants.normal_texture_index = ResolveOptionalTextureIndex(
    material.resolved_asset->GetNormalTextureKey());
  constants.metallic_texture_index = ResolveOptionalTextureIndex(
    material.resolved_asset->GetMetallicTextureKey());
  constants.roughness_texture_index = ResolveOptionalTextureIndex(
    material.resolved_asset->GetRoughnessTextureKey());
  constants.ambient_occlusion_texture_index = ResolveOptionalTextureIndex(
    material.resolved_asset->GetAmbientOcclusionTextureKey());

  // Copy flags; ensure alpha-test is set for masked domain.
  constants.flags = material.resolved_asset->GetFlags();
  if (material.resolved_asset->GetMaterialDomain()
    == oxygen::data::MaterialDomain::kMasked) {
    constants.flags |= oxygen::data::pak::kMaterialFlag_AlphaTest;
  }

  constants.alpha_cutoff = material.resolved_asset->GetAlphaCutoff();

  // Opacity is currently sourced from the base color texture alpha.
  // If texture sampling is disabled, keep it invalid to skip sampling.
  const auto alpha_test_enabled
    = (constants.flags & oxygen::data::pak::kMaterialFlag_AlphaTest) != 0U;
  if (alpha_test_enabled) {
    constants.opacity_texture_index = constants.base_color_texture_index;
  } else {
    constants.opacity_texture_index = oxygen::kInvalidShaderVisibleIndex;
  }

  const auto uv_scale = material.resolved_asset->GetUvScale();
  constants.uv_scale = { uv_scale[0], uv_scale[1] };

  const auto uv_offset = material.resolved_asset->GetUvOffset();
  constants.uv_offset = { uv_offset[0], uv_offset[1] };

  constants.uv_rotation_radians
    = material.resolved_asset->GetUvRotationRadians();
  constants.uv_set = material.resolved_asset->GetUvSet();

  if (material.resolved_asset->HasProceduralGrid()) {
    const auto grid_spacing = material.resolved_asset->GetGridSpacing();
    constants.grid_spacing = { grid_spacing[0], grid_spacing[1] };
    constants.grid_major_every = material.resolved_asset->GetGridMajorEvery();
    constants.grid_line_thickness
      = material.resolved_asset->GetGridLineThickness();
    constants.grid_major_thickness
      = material.resolved_asset->GetGridMajorThickness();
    constants.grid_axis_thickness
      = material.resolved_asset->GetGridAxisThickness();
    constants.grid_fade_start = material.resolved_asset->GetGridFadeStart();
    constants.grid_fade_end = material.resolved_asset->GetGridFadeEnd();

    const auto grid_minor_color = material.resolved_asset->GetGridMinorColor();
    constants.grid_minor_color = { grid_minor_color[0], grid_minor_color[1],
      grid_minor_color[2], grid_minor_color[3] };

    const auto grid_major_color = material.resolved_asset->GetGridMajorColor();
    constants.grid_major_color = { grid_major_color[0], grid_major_color[1],
      grid_major_color[2], grid_major_color[3] };

    const auto grid_axis_color_x = material.resolved_asset->GetGridAxisColorX();
    constants.grid_axis_color_x = { grid_axis_color_x[0], grid_axis_color_x[1],
      grid_axis_color_x[2], grid_axis_color_x[3] };

    const auto grid_axis_color_y = material.resolved_asset->GetGridAxisColorY();
    constants.grid_axis_color_y = { grid_axis_color_y[0], grid_axis_color_y[1],
      grid_axis_color_y[2], grid_axis_color_y[3] };

    const auto grid_origin_color
      = material.resolved_asset->GetGridOriginColor();
    constants.grid_origin_color = { grid_origin_color[0], grid_origin_color[1],
      grid_origin_color[2], grid_origin_color[3] };
  }

  // Emissive: factor and texture for self-illumination / glow.
  const auto emissive_factor = material.resolved_asset->GetEmissiveFactor();
  constants.emissive_factor
    = { emissive_factor[0], emissive_factor[1], emissive_factor[2] };
  constants.emissive_texture_index
    = ResolveTextureIndex(material.resolved_asset->GetEmissiveTextureKey(),
      material.resolved_asset->GetEmissiveTexture());

  return constants;
}

} // namespace

namespace oxygen::renderer::resources {

//=== MaterialBinder Implementation ========================================//

class MaterialBinder::Impl {
public:
  Impl(observer_ptr<Graphics> gfx,
    observer_ptr<engine::upload::UploadCoordinator> uploader,
    observer_ptr<engine::upload::StagingProvider> provider,
    observer_ptr<IResourceBinder> texture_binder,
    observer_ptr<content::IAssetLoader> asset_loader);

  ~Impl();

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  auto OnFrameStart(RendererTag /*tag*/, frame::Slot slot) -> void;
  auto GetOrAllocate(const engine::sceneprep::MaterialRef& material)
    -> engine::sceneprep::MaterialHandle;
  auto Update(engine::sceneprep::MaterialHandle handle,
    std::shared_ptr<const data::MaterialAsset> material) -> void;
  [[nodiscard]] auto IsHandleValid(
    engine::sceneprep::MaterialHandle handle) const -> bool;
  [[nodiscard]] auto GetMaterialConstants() const noexcept
    -> std::span<const engine::MaterialConstants>;
  [[nodiscard]] auto GetDirtyIndices() const noexcept
    -> std::span<const std::uint32_t>;
  auto EnsureFrameResources() -> void;
  auto OverrideUvTransform(const data::MaterialAsset& material,
    glm::vec2 uv_scale, glm::vec2 uv_offset) -> bool;
  [[nodiscard]] auto GetMaterialsSrvIndex() const -> ShaderVisibleIndex;

private:
  using ReuseStrategy = nexus::FrameDrivenIndexReuse<bindless::HeapIndex>;
  using VersionedIndex = nexus::VersionedIndex<bindless::HeapIndex>;

  [[nodiscard]] static auto ToVersionedIndex_(
    engine::sceneprep::MaterialHandle handle) -> VersionedIndex;
  [[nodiscard]] auto TryGetCurrentIndex_(
    engine::sceneprep::MaterialHandle handle) const
    -> std::optional<std::uint32_t>;

  struct MaterialCacheEntry {
    engine::sceneprep::MaterialHandle handle;
    std::uint32_t index;
  };
  struct CallbackGate {
    std::mutex mutex;
    bool alive { true };
  };
  struct PendingMaterialEviction {
    data::AssetKey asset_key {};
    content::EvictionReason reason { content::EvictionReason::kRefCountZero };
  };

  auto MarkDirty(std::uint32_t index) -> void;
  auto MarkAllDirty() -> void;
  [[nodiscard]] auto FindIndexByHandle(
    engine::sceneprep::MaterialHandle handle) const
    -> std::optional<std::uint32_t>;
  [[nodiscard]] auto EnsureAtlasCapacityOrLog(std::uint32_t desired_count)
    -> bool;

  auto UpdateKeyMappingForIndex(std::uint32_t index, std::uint64_t new_key)
    -> void;
  auto ProcessEvictions() -> void;

  // Deduplication and state
  std::unordered_map<std::uint64_t, MaterialCacheEntry> material_key_to_handle_;
  std::unordered_map<const data::MaterialAsset*, std::uint32_t>
    material_ptr_to_index_;
  std::vector<std::shared_ptr<const data::MaterialAsset>> materials_;
  std::vector<engine::MaterialConstants> material_constants_;
  std::vector<std::uint64_t> material_keys_;
  std::vector<std::uint32_t> material_handle_generations_;
  std::vector<std::uint32_t> dirty_epoch_;
  std::vector<std::uint32_t> dirty_indices_;
  std::uint32_t current_epoch_ { 1U }; // 0 reserved for 'never'
  std::uint32_t next_handle_index_ { 0U };
  std::shared_ptr<std::vector<bindless::HeapIndex>> free_indices_;

  // Statistics tracking
  std::uint64_t frames_started_count_ { 0U };
  std::uint64_t total_calls_ { 0U };
  std::uint64_t cache_hits_ { 0U };
  std::uint64_t total_allocations_ { 0U };
  std::uint64_t atlas_allocations_ { 0U };
  std::uint64_t upload_operations_ { 0U };

  // GPU upload dependencies
  observer_ptr<Graphics> gfx_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;
  observer_ptr<engine::upload::StagingProvider> staging_provider_;
  observer_ptr<IResourceBinder> texture_binder_;
  observer_ptr<content::IAssetLoader> asset_loader_;
  std::shared_ptr<CallbackGate> callback_gate_;
  std::mutex eviction_mutex_;
  std::deque<PendingMaterialEviction> pending_evictions_;
  content::IAssetLoader::EvictionSubscription eviction_subscription_;

  // Atlas-based material storage
  std::unique_ptr<AtlasBuffer> materials_atlas_;
  std::vector<AtlasBuffer::ElementRef> material_refs_;

  // Current frame slot for atlas element retirement
  frame::Slot current_frame_slot_ { frame::kInvalidSlot };
  graphics::detail::DeferredReclaimer slot_reclaimer_;
  ReuseStrategy slot_reuse_;

  // Frame resource tracking
  bool uploaded_this_frame_ { false };
};

MaterialBinder::MaterialBinder(observer_ptr<Graphics> gfx,
  observer_ptr<engine::upload::UploadCoordinator> uploader,
  observer_ptr<engine::upload::StagingProvider> provider,
  observer_ptr<IResourceBinder> texture_binder,
  observer_ptr<content::IAssetLoader> asset_loader)
  : impl_(std::make_unique<Impl>(
      gfx, uploader, provider, texture_binder, asset_loader))
{
}

MaterialBinder::~MaterialBinder() = default;

auto MaterialBinder::OnFrameStart(const RendererTag tag, const frame::Slot slot)
  -> void
{
  impl_->OnFrameStart(tag, slot);
}

auto MaterialBinder::GetOrAllocate(
  const engine::sceneprep::MaterialRef& material)
  -> engine::sceneprep::MaterialHandle
{
  return impl_->GetOrAllocate(material);
}

auto MaterialBinder::Update(const engine::sceneprep::MaterialHandle handle,
  std::shared_ptr<const data::MaterialAsset> material) -> void
{
  impl_->Update(handle, std::move(material));
}

auto MaterialBinder::OverrideUvTransform(const data::MaterialAsset& material,
  const glm::vec2 uv_scale, const glm::vec2 uv_offset) -> bool
{
  return impl_->OverrideUvTransform(material, uv_scale, uv_offset);
}

auto MaterialBinder::IsHandleValid(
  const engine::sceneprep::MaterialHandle handle) const -> bool
{
  return impl_->IsHandleValid(handle);
}

auto MaterialBinder::GetMaterialConstants() const noexcept
  -> std::span<const engine::MaterialConstants>
{
  return impl_->GetMaterialConstants();
}

auto MaterialBinder::EnsureFrameResources() -> void
{
  impl_->EnsureFrameResources();
}

auto MaterialBinder::GetMaterialsSrvIndex() const -> ShaderVisibleIndex
{
  return impl_->GetMaterialsSrvIndex();
}

//=== MaterialBinder::Impl Implementation ==================================//

MaterialBinder::Impl::Impl(const observer_ptr<Graphics> gfx,
  const observer_ptr<engine::upload::UploadCoordinator> uploader,
  const observer_ptr<engine::upload::StagingProvider> provider,
  const observer_ptr<IResourceBinder> texture_binder,
  const observer_ptr<content::IAssetLoader> asset_loader)
  : free_indices_(std::make_shared<std::vector<bindless::HeapIndex>>())
  , gfx_(gfx)
  , uploader_(uploader)
  , staging_provider_(provider)
  , texture_binder_(texture_binder)
  , asset_loader_(asset_loader)
  , slot_reuse_(slot_reclaimer_,
      [free_indices = free_indices_](
        bindless::HeapIndex index, std::monostate /*unused*/) {
        if (free_indices) {
          free_indices->push_back(index);
        }
      })
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");
  DCHECK_NOTNULL_F(texture_binder_, "TextureBinder cannot be null");
  CHECK_NOTNULL_F(asset_loader_, "IAssetLoader cannot be null");

  materials_atlas_ = std::make_unique<AtlasBuffer>(gfx_,
    static_cast<std::uint32_t>(sizeof(engine::MaterialConstants)),
    "MaterialConstantsAtlas");
  CHECK_NOTNULL_F(materials_atlas_, "Failed to create material atlas buffer");

  callback_gate_ = std::make_shared<CallbackGate>();
  CHECK_NOTNULL_F(callback_gate_, "Failed to create callback gate");

  eviction_subscription_ = asset_loader_->SubscribeResourceEvictions(
    data::MaterialAsset::ClassTypeId(),
    [gate = callback_gate_, this](const content::EvictionEvent& event) -> void {
      if (!gate) {
        return;
      }

      std::scoped_lock lock(gate->mutex);
      if (!gate->alive) {
        return;
      }
      if (!event.asset_key.has_value()) {
        return;
      }

      std::scoped_lock eviction_lock(eviction_mutex_);
      pending_evictions_.push_back(PendingMaterialEviction {
        .asset_key = *event.asset_key,
        .reason = event.reason,
      });
    });
}

MaterialBinder::Impl::~Impl()
{
  if (callback_gate_) {
    std::scoped_lock lock(callback_gate_->mutex);
    callback_gate_->alive = false;
  }

  slot_reclaimer_.ProcessAllDeferredReleases();

  const auto telemetry = slot_reuse_.GetTelemetrySnapshot();
  const auto expected_zero_marker = [](const uint64_t value) -> const char* {
    return value == 0U ? " ✓" : " (expected 0) !";
  };

  LOG_SCOPE_F(INFO, "MaterialBinder Statistics");
  LOG_F(INFO, "frames started            : {}", frames_started_count_);
  LOG_F(INFO, "nexus.allocate_calls      : {}", telemetry.allocate_calls);
  LOG_F(INFO, "nexus.release_calls       : {}", telemetry.release_calls);
  LOG_F(INFO, "nexus.stale_reject_count  : {}{}", telemetry.stale_reject_count,
    expected_zero_marker(telemetry.stale_reject_count));
  LOG_F(INFO, "nexus.duplicate_rejects   : {}{}",
    telemetry.duplicate_reject_count,
    expected_zero_marker(telemetry.duplicate_reject_count));
  LOG_F(INFO, "nexus.reclaimed_count     : {}", telemetry.reclaimed_count);
  LOG_F(INFO, "nexus.pending_count       : {}{}", telemetry.pending_count,
    expected_zero_marker(telemetry.pending_count));
  LOG_F(INFO, "total calls       : {}", total_calls_);
  LOG_F(INFO, "cache hits        : {}", cache_hits_);
  LOG_F(INFO, "total allocations : {}", total_allocations_);
  LOG_F(INFO, "atlas allocations : {}", atlas_allocations_);
  LOG_F(INFO, "upload operations : {}", upload_operations_);
  LOG_F(INFO, "materials stored  : {}", materials_.size());

  if (materials_atlas_) {
    const auto ms = materials_atlas_->GetStats();
    LOG_SCOPE_F(INFO, "Materials Atlas Buffer");
    LOG_F(INFO, "ensure calls      : {}", ms.ensure_calls);
    LOG_F(INFO, "allocations       : {}", ms.allocations);
    LOG_F(INFO, "releases          : {}", ms.releases);
    LOG_F(INFO, "capacity elements : {}", ms.capacity_elements);
    LOG_F(INFO, "next index        : {}", ms.next_index);
    LOG_F(INFO, "free list size    : {}", ms.free_list_size);
  }
}

auto MaterialBinder::Impl::OnFrameStart(
  RendererTag /*tag*/, const frame::Slot slot) -> void
{
  slot_reuse_.OnBeginFrame(slot);
  ++frames_started_count_;

  ++current_epoch_;
  if (current_epoch_ == 0U) {
    LOG_F(WARNING,
      "MaterialBinder::OnFrameStart - epoch overflow, resetting dirty state");
    current_epoch_ = 1U;
    std::ranges::fill(dirty_epoch_, 0U);
  }

  current_frame_slot_ = slot;
  dirty_indices_.clear();

  if (materials_atlas_) {
    materials_atlas_->OnFrameStart(slot);
  }
  ProcessEvictions();

  uploaded_this_frame_ = false;
}

auto MaterialBinder::Impl::ToVersionedIndex_(
  const engine::sceneprep::MaterialHandle handle) -> VersionedIndex
{
  return {
    .index = bindless::HeapIndex { handle.get() },
    .generation = handle.GenerationValue(),
  };
}

auto MaterialBinder::Impl::TryGetCurrentIndex_(
  const engine::sceneprep::MaterialHandle handle) const
  -> std::optional<std::uint32_t>
{
  if (!handle.IsValid()) {
    return std::nullopt;
  }
  if (!slot_reuse_.IsHandleCurrent(ToVersionedIndex_(handle))) {
    return std::nullopt;
  }
  const auto u_index = handle.get();
  if (u_index >= material_handle_generations_.size()) {
    return std::nullopt;
  }
  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  if (material_handle_generations_[u_index] != handle.GenerationValue().get()) {
    return std::nullopt;
  }
  return u_index;
}

auto MaterialBinder::Impl::FindIndexByHandle(
  const engine::sceneprep::MaterialHandle handle) const
  -> std::optional<std::uint32_t>
{
  const auto maybe_index = TryGetCurrentIndex_(handle);
  if (!maybe_index.has_value()) {
    return std::nullopt;
  }
  const auto u_index = *maybe_index;
  if (u_index >= materials_.size()) {
    return std::nullopt;
  }
  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  if (!materials_[u_index]) {
    return std::nullopt;
  }
  return u_index;
}

auto MaterialBinder::Impl::IsHandleValid(
  const engine::sceneprep::MaterialHandle handle) const -> bool
{
  return FindIndexByHandle(handle).has_value();
}

auto MaterialBinder::Impl::GetMaterialConstants() const noexcept
  -> std::span<const engine::MaterialConstants>
{
  return { material_constants_.data(), material_constants_.size() };
}

auto MaterialBinder::Impl::GetDirtyIndices() const noexcept
  -> std::span<const std::uint32_t>
{
  return { dirty_indices_.data(), dirty_indices_.size() };
}

auto MaterialBinder::Impl::MarkDirty(const std::uint32_t index) -> void
{
  if (index >= dirty_epoch_.size()) {
    dirty_epoch_.resize(static_cast<std::size_t>(index) + 1U, 0U);
  }
  // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
  if (dirty_epoch_[index] == current_epoch_) {
    return;
  }
  dirty_epoch_[index] = current_epoch_;
  dirty_indices_.push_back(index);
  uploaded_this_frame_ = false;
  // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)
}

auto MaterialBinder::Impl::MarkAllDirty() -> void
{
  const auto count = static_cast<std::uint32_t>(material_constants_.size());
  for (std::uint32_t i = 0U; i < count; ++i) {
    MarkDirty(i);
  }
}

auto MaterialBinder::Impl::EnsureAtlasCapacityOrLog(
  const std::uint32_t desired_count) -> bool
{
  DCHECK_NOTNULL_F(materials_atlas_.get(), "Atlas not initialized");
  const auto result = materials_atlas_->EnsureCapacity(desired_count, 0.5F);
  if (!result) {
    LOG_F(ERROR, "Failed to ensure material atlas capacity: {}",
      result.error().message());
    return false;
  }

  // AtlasBuffer does not migrate live data on resize (by design). If it was
  // created/resized, previously uploaded material constants are no longer
  // guaranteed to be present in GPU memory, so force a full re-upload.
  if (*result != engine::upload::EnsureBufferResult::kUnchanged) {
    MarkAllDirty();
  }

  return true;
}

auto MaterialBinder::Impl::UpdateKeyMappingForIndex(
  const std::uint32_t index, const std::uint64_t new_key) -> void
{
  if (index >= material_keys_.size()) {
    material_keys_.resize(static_cast<std::size_t>(index) + 1U, 0U);
  }
  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  const auto old_key = material_keys_[index];
  if (old_key != 0U) {
    const auto it = material_key_to_handle_.find(old_key);
    if (it != material_key_to_handle_.end() && it->second.index == index) {
      material_key_to_handle_.erase(it);
    }
  }

  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  material_keys_[index] = new_key;

  if (index >= material_handle_generations_.size()) {
    material_handle_generations_.resize(
      static_cast<std::size_t>(index) + 1U, 0U);
  }
  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  const auto handle_generation = material_handle_generations_[index];
  const auto handle = engine::sceneprep::MaterialHandle {
    engine::sceneprep::MaterialHandle::Index { index },
    engine::sceneprep::MaterialHandle::Generation { handle_generation },
  };

  // Canonical-first: if the key already exists, do not remap it. This ensures
  // GetOrAllocate(key) keeps returning the original handle.
  const auto [it, inserted] = material_key_to_handle_.try_emplace(
    new_key, MaterialCacheEntry { .handle = handle, .index = index });
  if (!inserted) {
    DLOG_F(4,
      "MaterialBinder: key {} already mapped to handle {}; ignoring remap to "
      "handle {}",
      new_key, it->second.handle.get(), handle.get());
  }
}

auto MaterialBinder::Impl::GetOrAllocate(
  const engine::sceneprep::MaterialRef& material)
  -> engine::sceneprep::MaterialHandle
{
  ++total_calls_;

  if (!material.resolved_asset) {
    LOG_F(WARNING,
      "MaterialBinder::GetOrAllocate: null resolved material (source_key={}, "
      "resolved_key={})",
      oxygen::data::to_string(material.source_asset_key),
      oxygen::data::to_string(material.resolved_asset_key));
    return engine::sceneprep::kInvalidMaterialHandle;
  }

  std::string error_msg;
  if (!ValidateMaterial(*material.resolved_asset, error_msg)) {
    LOG_F(ERROR,
      "Material validation failed: {} (source_key={}, resolved_key={})",
      error_msg, oxygen::data::to_string(material.source_asset_key),
      oxygen::data::to_string(material.resolved_asset_key));
    return engine::sceneprep::kInvalidMaterialHandle;
  }

  const auto key = MakeMaterialKey(material);
  if (const auto it = material_key_to_handle_.find(key);
    it != material_key_to_handle_.end()) {
    const auto maybe_cached_index = FindIndexByHandle(it->second.handle);
    if (!maybe_cached_index.has_value()) {
      material_key_to_handle_.erase(it);
    } else {
      const auto idx = *maybe_cached_index;
      if (idx >= materials_.size()) {
        LOG_F(ERROR,
          "MaterialBinder: cached index out of range for key {} "
          "(source_key={}, resolved_key={})",
          key, oxygen::data::to_string(material.source_asset_key),
          oxygen::data::to_string(material.resolved_asset_key));
        return engine::sceneprep::kInvalidMaterialHandle;
      }

      ++cache_hits_;

      // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
      const auto* old_ptr = materials_[idx].get();
      if ((old_ptr != nullptr) && old_ptr != material.resolved_asset.get()) {
        material_ptr_to_index_.erase(old_ptr);
      }
      // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
      materials_[idx] = material.resolved_asset;
      material_ptr_to_index_[material.resolved_asset.get()] = idx;

      const auto& cached_asset = *material.resolved_asset;
      const bool no_texture_sampling
        = (cached_asset.GetFlags()
            & oxygen::data::pak::kMaterialFlag_NoTextureSampling)
        != 0U;
      if (!no_texture_sampling) {
        const auto needs_refresh
          = [this](const content::ResourceKey key) -> bool {
          return key.get() != 0U && !texture_binder_->IsResourceReady(key);
        };

        if (needs_refresh(cached_asset.GetBaseColorTextureKey())
          || needs_refresh(cached_asset.GetNormalTextureKey())
          || needs_refresh(cached_asset.GetMetallicTextureKey())
          || needs_refresh(cached_asset.GetRoughnessTextureKey())
          || needs_refresh(cached_asset.GetAmbientOcclusionTextureKey())
          || needs_refresh(cached_asset.GetEmissiveTextureKey())
          || needs_refresh(cached_asset.GetSpecularTextureKey())
          || needs_refresh(cached_asset.GetSheenColorTextureKey())
          || needs_refresh(cached_asset.GetClearcoatTextureKey())
          || needs_refresh(cached_asset.GetClearcoatNormalTextureKey())
          || needs_refresh(cached_asset.GetTransmissionTextureKey())
          || needs_refresh(cached_asset.GetThicknessTextureKey())) {
          material_constants_[idx]
            = SerializeMaterialConstants(material, *texture_binder_);
          MarkDirty(idx);
        }
      }

      return it->second.handle;
    }
  }

  std::uint32_t index = 0U;
  if (free_indices_->empty()) {
    index = next_handle_index_++;
  } else {
    index = free_indices_->back().get();
    free_indices_->pop_back();
  }
  const auto constants = SerializeMaterialConstants(material, *texture_binder_);

  // Ensure atlas has capacity before allocating the element ref.
  if (!EnsureAtlasCapacityOrLog(index + 1U)) {
    return engine::sceneprep::kInvalidMaterialHandle;
  }

  auto ref = materials_atlas_->Allocate(1);
  if (!ref.has_value()) {
    LOG_F(ERROR, "Failed to allocate material atlas element");
    return engine::sceneprep::kInvalidMaterialHandle;
  }

  const auto versioned_handle
    = slot_reuse_.ActivateSlot(bindless::HeapIndex { index });
  const auto handle = engine::sceneprep::MaterialHandle {
    engine::sceneprep::MaterialHandle::Index { versioned_handle.index.get() },
    engine::sceneprep::MaterialHandle::Generation {
      versioned_handle.generation.get() },
  };

  if (materials_.size() <= index) {
    materials_.resize(static_cast<std::size_t>(index) + 1U);
    material_constants_.resize(static_cast<std::size_t>(index) + 1U);
    material_refs_.resize(static_cast<std::size_t>(index) + 1U);
  }
  if (material_handle_generations_.size() <= index) {
    material_handle_generations_.resize(
      static_cast<std::size_t>(index) + 1U, 0U);
  }

  // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
  materials_[index] = material.resolved_asset;
  material_constants_[index] = constants;
  material_refs_[index] = *ref;
  material_handle_generations_[index] = handle.GenerationValue().get();
  // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)
  material_ptr_to_index_[material.resolved_asset.get()] = index;
  ++total_allocations_;
  ++atlas_allocations_;

  UpdateKeyMappingForIndex(index, key);
  MarkDirty(index);

  return handle;
}

auto MaterialBinder::Impl::Update(
  const engine::sceneprep::MaterialHandle handle,
  std::shared_ptr<const data::MaterialAsset> material) -> void
{
  ++total_calls_;

  const auto maybe_index = FindIndexByHandle(handle);
  if (!maybe_index.has_value()) {
    LOG_F(WARNING, "Update received invalid handle {}", to_string(handle));
    return;
  }

  if (!material) {
    LOG_F(WARNING, "Update received null material for handle {}",
      to_string(handle));
    return;
  }

  std::string error_msg;
  if (!ValidateMaterial(*material, error_msg)) {
    LOG_F(ERROR, "Material update validation failed: {} (asset_key={})",
      error_msg, oxygen::data::to_string(material->GetAssetKey()));
    return;
  }

  const auto index = *maybe_index;
  // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
  engine::sceneprep::MaterialRef mat_ref {
    .source_asset_key = material->GetAssetKey(),
    .resolved_asset_key = material->GetAssetKey(),
    .resolved_asset = material,
  };
  const auto new_key = MakeMaterialKey(mat_ref);
  const auto new_constants
    = SerializeMaterialConstants(mat_ref, *texture_binder_);

  const auto* old_ptr = materials_[index].get();
  if (old_ptr != nullptr && old_ptr != material.get()) {
    material_ptr_to_index_.erase(old_ptr);
  }
  materials_[index] = std::move(material);
  material_ptr_to_index_[materials_[index].get()] = index;

  material_constants_[index] = new_constants;
  UpdateKeyMappingForIndex(index, new_key);
  MarkDirty(index);
  // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)
}

auto MaterialBinder::Impl::ProcessEvictions() -> void
{
  std::deque<PendingMaterialEviction> evictions;
  {
    std::scoped_lock lock(eviction_mutex_);
    if (pending_evictions_.empty()) {
      return;
    }
    evictions.swap(pending_evictions_);
  }

  if (current_frame_slot_ == frame::kInvalidSlot) {
    std::scoped_lock lock(eviction_mutex_);
    pending_evictions_.insert(
      pending_evictions_.end(), evictions.begin(), evictions.end());
    return;
  }

  std::size_t evictions_without_entry = 0U;

  for (const auto& eviction : evictions) {
    bool found_match = false;
    for (std::size_t entry_index = 0U; entry_index < materials_.size();
      ++entry_index) {
      // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
      const auto& material = materials_[entry_index];
      if (!material || material->GetAssetKey() != eviction.asset_key) {
        continue;
      }
      found_match = true;

      if (entry_index < material_keys_.size()) {
        // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
        const auto key = material_keys_[entry_index];
        if (key != 0U) {
          const auto it = material_key_to_handle_.find(key);
          if (it != material_key_to_handle_.end()
            && it->second.index == static_cast<std::uint32_t>(entry_index)) {
            material_key_to_handle_.erase(it);
          }
          // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
          material_keys_[entry_index] = 0U;
        }
      }

      material_ptr_to_index_.erase(material.get());

      if (entry_index < material_refs_.size()) {
        // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
        materials_atlas_->Release(
          material_refs_[entry_index], current_frame_slot_);
        // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
        material_refs_[entry_index] = AtlasBuffer::ElementRef {};
      }

      if (entry_index < material_handle_generations_.size()) {
        // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
        const auto generation = material_handle_generations_[entry_index];
        if (generation != 0U) {
          const auto handle = engine::sceneprep::MaterialHandle {
            engine::sceneprep::MaterialHandle::Index {
              static_cast<std::uint32_t>(entry_index) },
            engine::sceneprep::MaterialHandle::Generation { generation },
          };
          slot_reuse_.Release(ToVersionedIndex_(handle));
        }
        // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
        material_handle_generations_[entry_index] = 0U;
      }

      // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
      materials_[entry_index].reset();
      if (entry_index < material_constants_.size()) {
        // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
        material_constants_[entry_index] = engine::MaterialConstants {};
      }
      if (entry_index < dirty_epoch_.size()) {
        // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
        dirty_epoch_[entry_index] = 0U;
      }
      dirty_indices_.erase(
        std::remove(dirty_indices_.begin(), dirty_indices_.end(),
          static_cast<std::uint32_t>(entry_index)),
        dirty_indices_.end());

      LOG_F(2, "MaterialBinder: eviction processed for {} (reason={})",
        data::to_string(eviction.asset_key), eviction.reason);
    }

    if (!found_match) {
      ++evictions_without_entry;
    }
  }

  if (evictions_without_entry != 0U) {
    LOG_F(INFO, "MaterialBinder: {} eviction(s) had no resident material entry",
      evictions_without_entry);
  }
}

auto MaterialBinder::Impl::OverrideUvTransform(
  const data::MaterialAsset& material, const glm::vec2 uv_scale,
  const glm::vec2 uv_offset) -> bool
{
  const auto ValidateScale = [](const glm::vec2 v) -> bool {
    // Allow negative scale to support common mirroring operations (e.g.
    // v' = 1 - v). Only require non-zero, finite values.
    constexpr float kMinAbsScale = 1e-6F;
    return std::isfinite(v.x) && std::isfinite(v.y)
      && std::abs(v.x) > kMinAbsScale && std::abs(v.y) > kMinAbsScale;
  };
  const auto ValidateOffset = [](const glm::vec2 v) -> bool {
    return std::isfinite(v.x) && std::isfinite(v.y);
  };

  if (!ValidateScale(uv_scale) || !ValidateOffset(uv_offset)) {
    LOG_F(WARNING,
      "OverrideUvTransform: invalid values (scale=({},{}), offset=({},{}))",
      uv_scale.x, uv_scale.y, uv_offset.x, uv_offset.y);
    return false;
  }

  const auto it = material_ptr_to_index_.find(&material);
  if (it == material_ptr_to_index_.end()) {
    DLOG_F(4, "OverrideUvTransform: material not found (asset_key={})",
      oxygen::data::to_string(material.GetAssetKey()));
    return false;
  }

  const auto index = it->second;
  if (index >= material_constants_.size()) {
    return false;
  }

  // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
  auto& constants = material_constants_[index];
  constants.uv_scale = uv_scale;
  constants.uv_offset = uv_offset;
  MarkDirty(index);
  return true;
}

auto MaterialBinder::Impl::EnsureFrameResources() -> void
{
  if (uploaded_this_frame_) {
    return;
  }

  if (current_frame_slot_ == frame::kInvalidSlot) {
    LOG_F(ERROR,
      "EnsureFrameResources() called before OnFrameStart() - frame lifecycle "
      "violation");
    return;
  }

  // Ensure SRV exists even when no uploads are required.
  const auto desired = std::max<std::uint32_t>(
    1U, static_cast<std::uint32_t>(materials_.size()));
  if (!EnsureAtlasCapacityOrLog(desired)) {
    return;
  }

  if (dirty_indices_.empty() || materials_.empty()) {
    uploaded_this_frame_ = true;
    return;
  }

  std::vector<engine::upload::UploadRequest> requests;
  requests.reserve(dirty_indices_.size());

  const auto stride = sizeof(engine::MaterialConstants);
  for (const auto index : dirty_indices_) {
    if (index >= material_refs_.size() || index >= material_constants_.size()) {
      LOG_F(ERROR, "MaterialBinder: dirty index out of range: {}", index);
      continue;
    }

    // IMPORTANT: AtlasBuffer may recreate its SRV during growth. ElementRef
    // stores the SRV index that was current at allocation time, which can
    // become stale across resizes. Use index-based descriptors to avoid SRV
    // mismatches when reuploading.
    auto desc = materials_atlas_->MakeUploadDescForIndex(index, stride);
    if (!desc.has_value()) {
      LOG_F(ERROR, "Failed to create upload descriptor for material {}", index);
      continue;
    }

    engine::upload::UploadRequest req;
    req.kind = engine::upload::UploadKind::kBuffer;
    req.debug_name = "MaterialConstants";
    req.desc = *desc;
    req.data = engine::upload::UploadDataView {
      // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
      std::as_bytes(
        std::span<engine::MaterialConstants>(&material_constants_[index], 1))
        .first(stride)
    };
    requests.push_back(std::move(req));
  }

  if (requests.empty()) {
    uploaded_this_frame_ = true;
    return;
  }

  const auto tickets = uploader_->SubmitMany(
    std::span { requests.data(), requests.size() }, *staging_provider_);
  upload_operations_ += requests.size();

  if (!tickets.has_value()) {
    const std::error_code ec = tickets.error();
    LOG_F(ERROR, "Material upload submission failed: [{}] {}",
      ec.category().name(), ec.message());
    return;
  }

  if (tickets->size() != requests.size()) {
    LOG_F(ERROR,
      "Material upload submission partial failure: expected {} tickets, got "
      "{}",
      requests.size(), tickets->size());
  }

  uploaded_this_frame_ = true;
}

auto MaterialBinder::Impl::GetMaterialsSrvIndex() const -> ShaderVisibleIndex
{
  DCHECK_NOTNULL_F(materials_atlas_.get(), "Atlas not initialized");
  if (materials_atlas_->GetBinding().srv == kInvalidShaderVisibleIndex) {
    const auto desired = std::max<std::uint32_t>(
      1U, static_cast<std::uint32_t>(materials_.size()));
    const auto result = materials_atlas_->EnsureCapacity(desired, 0.5F);
    if (!result) {
      LOG_F(ERROR, "Failed to ensure material atlas capacity for SRV: {}",
        result.error().message());
      return kInvalidShaderVisibleIndex;
    }
  }
  return materials_atlas_->GetBinding().srv;
}

} // namespace oxygen::renderer::resources
