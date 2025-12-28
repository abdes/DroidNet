//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Resources/ITextureBinder.h>
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/ScenePrep/MaterialRef.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

using oxygen::engine::upload::AtlasBuffer;

namespace {

//! Validate material asset for common issues and constraints.
[[nodiscard]] auto ValidateMaterial(
  const oxygen::data::MaterialAsset& material, std::string& error_msg) -> bool
{
  // Check for reasonable scalar values
  const auto base_color = material.GetBaseColor();
  for (int i = 0; i < 4; ++i) {
    if (!std::isfinite(base_color[i])) {
      error_msg = "Material base_color contains non-finite values";
      return false;
    }
    if (base_color[i] < 0.0f || base_color[i] > 10.0f) {
      error_msg = "Material base_color values out of reasonable range [0, 10]";
      return false;
    }
  }

  const auto metalness = material.GetMetalness();
  if (!std::isfinite(metalness) || metalness < 0.0f || metalness > 1.0f) {
    error_msg = "Material metalness out of valid range [0, 1]";
    return false;
  }

  const auto roughness = material.GetRoughness();
  if (!std::isfinite(roughness) || roughness < 0.0f || roughness > 1.0f) {
    error_msg = "Material roughness out of valid range [0, 1]";
    return false;
  }

  const auto normal_scale = material.GetNormalScale();
  if (!std::isfinite(normal_scale) || normal_scale < 0.0f
    || normal_scale > 10.0f) {
    error_msg = "Material normal_scale out of reasonable range [0, 10]";
    return false;
  }

  const auto ambient_occlusion = material.GetAmbientOcclusion();
  if (!std::isfinite(ambient_occlusion) || ambient_occlusion < 0.0f
    || ambient_occlusion > 1.0f) {
    error_msg = "Material ambient_occlusion out of valid range [0, 1]";
    return false;
  }

  return true;
}

//! Create a content-based hash key for material deduplication.
auto MakeMaterialKey(
  const oxygen::engine::sceneprep::MaterialRef& material) noexcept
  -> std::uint64_t
{
  // Hash based on key material properties for deduplication
  const auto base_color = material.asset->GetBaseColor();
  std::uint64_t hash = 0;

  // Hash base color components (quantized for stability)
  constexpr float color_scale = 1024.0f;
  for (int i = 0; i < 4; ++i) {
    const auto quantized
      = static_cast<std::uint32_t>(std::round(base_color[i] * color_scale))
      & 0xFFFF;
    hash ^= quantized << (i * 16);
  }

  // Hash scalar properties (quantized)
  constexpr float scalar_scale = 1024.0f;
  const auto metalness_q = static_cast<std::uint32_t>(std::round(
                             material.asset->GetMetalness() * scalar_scale))
    & 0xFFFF;
  const auto roughness_q = static_cast<std::uint32_t>(std::round(
                             material.asset->GetRoughness() * scalar_scale))
    & 0xFFFF;

  hash ^= (static_cast<std::uint64_t>(metalness_q) << 32)
    | (static_cast<std::uint64_t>(roughness_q) << 48);

  // Hash texture indices
  hash ^= material.asset->GetBaseColorTexture();
  hash ^= static_cast<std::uint64_t>(material.asset->GetNormalTexture()) << 8;
  hash ^= static_cast<std::uint64_t>(material.asset->GetMetallicTexture())
    << 16;
  hash ^= static_cast<std::uint64_t>(material.asset->GetRoughnessTexture())
    << 24;
  hash
    ^= static_cast<std::uint64_t>(material.asset->GetAmbientOcclusionTexture())
    << 32;

  // Hash flags
  hash ^= static_cast<std::uint64_t>(material.GetFlags()) << 40;

  return hash;
}

//! Serialize MaterialAsset data into MaterialConstants format.
auto SerializeMaterialConstants(
  const oxygen::engine::sceneprep::MaterialRef& material,
  oxygen::renderer::resources::ITextureBinder& texture_binder) noexcept
  -> oxygen::engine::MaterialConstants
{
  oxygen::engine::MaterialConstants constants;

  // Copy base color
  const auto base_color = material.asset->GetBaseColor();
  constants.base_color
    = { base_color[0], base_color[1], base_color[2], base_color[3] };

  // Copy scalar properties
  constants.metalness = material.asset->GetMetalness();
  constants.roughness = material.asset->GetRoughness();
  constants.normal_scale = material.asset->GetNormalScale();
  constants.ambient_occlusion = material.asset->GetAmbientOcclusion();

  // Resolve texture resource keys to SRV indices via TextureBinder
  constants.base_color_texture_index
    = texture_binder.GetOrAllocate(material.GetBaseColorTextureKey()).get();
  constants.normal_texture_index
    = texture_binder.GetOrAllocate(material.GetNormalTextureKey()).get();
  constants.metallic_texture_index
    = texture_binder.GetOrAllocate(material.GetMetallicTextureKey()).get();
  constants.roughness_texture_index
    = texture_binder.GetOrAllocate(material.GetRoughnessTextureKey()).get();
  constants.ambient_occlusion_texture_index
    = texture_binder.GetOrAllocate(material.GetAmbientOcclusionTextureKey())
        .get();

  LOG_F(INFO,
    "MaterialBinder: resolved texture SRV indices (base={}, normal={}, "
    "metal={}, roughness={}, ao={})",
    constants.base_color_texture_index, constants.normal_texture_index,
    constants.metallic_texture_index, constants.roughness_texture_index,
    constants.ambient_occlusion_texture_index);

  // Copy flags
  constants.flags = material.GetFlags();

  // Default UV transform (identity). Runtime/editor can override via
  // MaterialBinder::OverrideUvTransform.
  constants.uv_scale = { 1.0F, 1.0F };
  constants.uv_offset = { 0.0F, 0.0F };

  // Padding fields are already initialized to 0
  constants._pad0 = 0;
  constants._pad1 = 0;

  return constants;
}

} // namespace

namespace oxygen::renderer::resources {

MaterialBinder::MaterialBinder(observer_ptr<Graphics> gfx,
  observer_ptr<engine::upload::UploadCoordinator> uploader,
  observer_ptr<engine::upload::StagingProvider> provider,
  observer_ptr<ITextureBinder> texture_binder)
  : gfx_(std::move(gfx))
  , uploader_(std::move(uploader))
  , staging_provider_(std::move(provider))
  , texture_binder_(std::move(texture_binder))
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");
  DCHECK_NOTNULL_F(texture_binder_, "TextureBinder cannot be null");

  // Prepare atlas buffer for material constants storage
  materials_atlas_ = std::make_unique<AtlasBuffer>(gfx_,
    static_cast<std::uint32_t>(sizeof(engine::MaterialConstants)),
    "MaterialConstantsAtlas");
}

MaterialBinder::~MaterialBinder()
{
  LOG_SCOPE_F(INFO, "MaterialBinder Statistics");
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

auto MaterialBinder::OnFrameStart(
  renderer::RendererTag, oxygen::frame::Slot slot) -> void
{
  ++current_epoch_;
  if (current_epoch_ == 0U) {
    // Epoch overflow - reset all dirty tracking
    LOG_F(WARNING,
      "MaterialBinder::OnFrameStart - epoch overflow, resetting all dirty "
      "tracking");
    current_epoch_ = 1U;
    std::fill(dirty_epoch_.begin(), dirty_epoch_.end(), 0U);
  }

  frame_write_count_ = 0U;
  current_frame_slot_ = slot;
  dirty_indices_.clear();

  // Phase 1: Keep cache across frames for stable handle indices.
  // Cache clearing violated Phase 1 "stable entries" requirement.
  // The cache enables cross-frame material deduplication and handle stability.

  // Prepare atlas lifecycle (recycle any retired elements for this slot)
  if (materials_atlas_) {
    materials_atlas_->OnFrameStart(slot);
  }

  // Reset frame resource tracking
  uploaded_this_frame_ = false;
}

auto MaterialBinder::GetOrAllocate(
  const oxygen::engine::sceneprep::MaterialRef& material)
  -> engine::sceneprep::MaterialHandle
{
  ++total_calls_;

  // Handle null materials - return invalid handle that will fail IsValidHandle
  if (!material.asset) {
    return engine::sceneprep::kInvalidMaterialHandle;
  }

  // Validate material
  std::string error_msg;
  if (!ValidateMaterial(*material.asset, error_msg)) {
    LOG_F(ERROR, "Material validation failed: {}", error_msg);
    return engine::sceneprep::kInvalidMaterialHandle;
  }

  const auto key = MakeMaterialKey(material);

  if (const auto it = material_key_to_handle_.find(key);
    it != material_key_to_handle_.end()) {
    // Found existing handle - verify it's the same material to handle hash
    // collisions
    const auto& entry = it->second;
    const auto cached_handle = entry.handle;
    const auto idx = entry.index;
    if (idx < materials_.size()
      && materials_[idx].get() == material.asset.get()) {
      // Same material, cache hit - only count when actually reusing existing
      // storage
      ++cache_hits_;
      if (dirty_epoch_[idx] != current_epoch_) {
        dirty_epoch_[idx] = current_epoch_;
        dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
      }
      // Consume one write slot this frame to keep order stable.
      ++frame_write_count_;
      return cached_handle; // Same material, return existing handle
    }
    // Hash collision or changed value - treat as same logical material whose
    // value changed; update in-place to keep handle stable and mark dirty.
    // This is NOT a cache hit since the material content changed.
    const auto* old_ptr = materials_[idx].get();
    materials_[idx] = material.asset;
    if (old_ptr && material_ptr_to_index_.count(old_ptr) != 0U
      && material_ptr_to_index_[old_ptr] == static_cast<std::uint32_t>(idx)) {
      material_ptr_to_index_.erase(old_ptr);
    }
    material_ptr_to_index_[material.asset.get()]
      = static_cast<std::uint32_t>(idx);
    material_constants_[idx]
      = SerializeMaterialConstants(material, *texture_binder_);
    if (idx >= dirty_epoch_.size()) {
      dirty_epoch_.resize(idx + 1U, 0U);
    }
    dirty_epoch_[idx] = current_epoch_;
    if (std::find(dirty_indices_.begin(), dirty_indices_.end(),
          static_cast<std::uint32_t>(idx))
      == dirty_indices_.end()) {
      dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
    }
    // Rebind new key to same handle for any subsequent calls this frame.
    material_key_to_handle_[key]
      = MaterialCacheEntry { cached_handle, static_cast<std::uint32_t>(idx) };
    ++frame_write_count_;
    return cached_handle;
  }

  // No cache hit: either a brand new logical material, or a changed value
  // that should reuse an existing slot this frame. Reuse by frame order.
  const bool is_new_logical = frame_write_count_ >= materials_.size();
  std::uint32_t index = 0;

  if (is_new_logical) {
    // Append new entries
    materials_.push_back(material.asset);
    material_constants_.push_back(
      SerializeMaterialConstants(material, *texture_binder_));
    dirty_epoch_.push_back(current_epoch_);
    index = static_cast<std::uint32_t>(materials_.size() - 1);
    material_ptr_to_index_[material.asset.get()] = index;
    ++total_allocations_; // Track logical allocation of new material
  } else {
    // Reuse existing slot in this frame by order; mark dirty and update.
    index = frame_write_count_;
    const auto* old_ptr = materials_[index].get();
    materials_[index] = material.asset;
    if (old_ptr && material_ptr_to_index_.count(old_ptr) != 0U
      && material_ptr_to_index_[old_ptr] == index) {
      material_ptr_to_index_.erase(old_ptr);
    }
    material_ptr_to_index_[material.asset.get()] = index;
    material_constants_[index]
      = SerializeMaterialConstants(material, *texture_binder_);
    if (index >= dirty_epoch_.size()) {
      dirty_epoch_.resize(index + 1U, 0U);
    }
    dirty_epoch_[index] = current_epoch_;
  }

  // Ensure atlas capacity and allocate one element
  if (const auto result = materials_atlas_->EnsureCapacity(
        static_cast<std::uint32_t>(materials_.size()), 0.5f);
    !result) {
    LOG_F(ERROR, "Failed to ensure material atlas capacity: {}",
      result.error().message());
    return engine::sceneprep::kInvalidMaterialHandle;
  }

  // Ensure element refs array is sized; allocate refs only when new
  if (material_refs_.size() < materials_.size()) {
    const auto need
      = static_cast<std::uint32_t>(materials_.size() - material_refs_.size());
    for (std::uint32_t n = 0; n < need; ++n) {
      auto ref = materials_atlas_->Allocate(1);
      DCHECK_F(ref.has_value(), "Failed to allocate material (atlas)");
      material_refs_.push_back(*ref);
      ++atlas_allocations_; // Track atlas element allocation
    }
  }

  const auto handle = engine::sceneprep::MaterialHandle { index };

  // Mark dirty for this frame
  if (std::find(dirty_indices_.begin(), dirty_indices_.end(), index)
    == dirty_indices_.end()) {
    dirty_indices_.push_back(index);
  }

  // Release old atlas allocation if we're replacing an existing cache entry
  if (auto it = material_key_to_handle_.find(key);
    it != material_key_to_handle_.end()) {
    const auto old_index = it->second.index;
    if (old_index < material_refs_.size()) {
      materials_atlas_->Release(material_refs_[old_index], current_frame_slot_);
    }
  }

  // Cache for deduplication: map value key to this handle and index
  material_key_to_handle_[key] = MaterialCacheEntry { handle, index };
  if (is_new_logical) {
    ++total_allocations_;
  }
  ++frame_write_count_;

  return handle;
}

auto MaterialBinder::Update(engine::sceneprep::MaterialHandle handle,
  std::shared_ptr<const data::MaterialAsset> material) -> void
{
  ++total_calls_; // Track Update calls along with GetOrAllocate calls

  const auto idx = static_cast<std::size_t>(handle.get());
  if (!IsValidHandle(handle)) {
    LOG_F(WARNING, "Update received invalid handle {}", handle.get());
    return;
  }

  // Handle null material updates
  if (!material) {
    LOG_F(WARNING, "Update received null material for handle {}", handle.get());
    return;
  }

  // Validate the new material data
  std::string error_msg;
  if (!ValidateMaterial(*material, error_msg)) {
    LOG_F(ERROR, "Material update validation failed: {}", error_msg);
    return;
  }

  // Check if the material actually changed (by pointer comparison)
  if (materials_[idx].get() == material.get()) {
    ++cache_hits_; // Count as cache hit when no update needed
    return; // Same material, no update needed
  }

  const auto* old_ptr = materials_[idx].get();
  materials_[idx] = material;
  if (old_ptr && material_ptr_to_index_.count(old_ptr) != 0U
    && material_ptr_to_index_[old_ptr] == static_cast<std::uint32_t>(idx)) {
    material_ptr_to_index_.erase(old_ptr);
  }
  material_ptr_to_index_[material.get()] = static_cast<std::uint32_t>(idx);
  // Use MaterialRef wrapper to serialize constants (contains opaque
  // ResourceKeys)
  engine::sceneprep::MaterialRef mat_ref { std::move(material) };
  material_constants_[idx]
    = SerializeMaterialConstants(mat_ref, *texture_binder_);

  // Mark dirty for this frame
  if (dirty_epoch_[idx] != current_epoch_) {
    dirty_epoch_[idx] = current_epoch_;
    dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
  }
}

auto MaterialBinder::OverrideUvTransform(const data::MaterialAsset& material,
  const glm::vec2 uv_scale, const glm::vec2 uv_offset) -> bool
{
  // TODO: Replace this shared-material mutation with MaterialInstance
  // support (or per-draw instance constants) so per-object UV overrides do not
  // affect other objects that share the same MaterialAsset.

  const auto ValidateScale = [](const glm::vec2 v) -> bool {
    return std::isfinite(v.x) && std::isfinite(v.y) && v.x > 0.0F && v.y > 0.0F;
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
    return false;
  }

  const auto idx = static_cast<std::size_t>(it->second);
  if (idx >= material_constants_.size()) {
    return false;
  }

  auto& constants = material_constants_[idx];
  constants.uv_scale = uv_scale;
  constants.uv_offset = uv_offset;

  if (idx >= dirty_epoch_.size()) {
    dirty_epoch_.resize(idx + 1U, 0U);
  }
  dirty_epoch_[idx] = current_epoch_;
  if (std::find(dirty_indices_.begin(), dirty_indices_.end(),
        static_cast<std::uint32_t>(idx))
    == dirty_indices_.end()) {
    dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
  }

  // If frame resources were already uploaded this frame, the new UV values will
  // be visible next frame unless EnsureFrameResources is called again.
  uploaded_this_frame_ = false;
  return true;
}

auto MaterialBinder::IsValidHandle(
  engine::sceneprep::MaterialHandle handle) const -> bool
{
  const auto idx = static_cast<std::size_t>(handle.get());
  return idx < materials_.size() && materials_[idx] != nullptr;
}

auto MaterialBinder::GetMaterialConstants() const noexcept
  -> std::span<const engine::MaterialConstants>
{
  return { material_constants_.data(), material_constants_.size() };
}

auto MaterialBinder::GetDirtyIndices() const noexcept
  -> std::span<const std::uint32_t>
{
  return { dirty_indices_.data(), dirty_indices_.size() };
}

auto MaterialBinder::EnsureFrameResources() -> void
{
  if (uploaded_this_frame_ || materials_.empty()) {
    return;
  }

  // Ensure SRV exists even if there are no new uploads this frame
  if (const auto result = materials_atlas_->EnsureCapacity(
        std::max<std::uint32_t>(
          1U, static_cast<std::uint32_t>(materials_.size())),
        0.5f);
    !result) {
    LOG_F(ERROR,
      "Failed to ensure material atlas capacity for frame resources: {}",
      result.error().message());
    return; // Skip uploads if capacity cannot be ensured
  }

  std::vector<oxygen::engine::upload::UploadRequest> requests;
  requests.reserve(8); // small, will grow if needed

  const auto stride
    = static_cast<std::uint64_t>(sizeof(engine::MaterialConstants));
  const auto count = static_cast<std::uint32_t>(materials_.size());

  // Emit per-element requests for dirty entries; coordinator will batch
  // and coalesce (including contiguous merges) across all buffer requests.
  // Phase 1: Upload all materials every frame (ignore dirty tracking)
  for (std::uint32_t i = 0; i < count; ++i) {
    // Phase 1 behavior: Upload all materials every frame regardless of dirty
    // status

    // const bool is_dirty = (i < dirty_epoch_.size()) &&
    // (dirty_epoch_[i] == current_epoch_); if (!is_dirty) {
    //   continue;
    // }

    // Material constants
    if (auto desc
      = materials_atlas_->MakeUploadDesc(material_refs_[i], stride)) {
      oxygen::engine::upload::UploadRequest req;
      req.kind = oxygen::engine::upload::UploadKind::kBuffer;
      req.debug_name = "MaterialConstants";
      req.desc = *desc;
      req.data = oxygen::engine::upload::UploadDataView {
        std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(&material_constants_[i]), stride)
      };
      requests.push_back(std::move(req));
    } else {
      LOG_F(ERROR, "Failed to create upload descriptor for material {}", i);
    }
  }

  if (!requests.empty()) {
    const auto tickets = uploader_->SubmitMany(
      std::span { requests.data(), requests.size() }, *staging_provider_);
    upload_operations_ += requests.size(); // Track number of upload operations

    // Handle upload submission result
    if (!tickets.has_value()) {
      std::error_code ec = tickets.error();
      LOG_F(ERROR, "Transform upload submission failed: [{}] {}",
        ec.category().name(), ec.message());
      return; // Early return on submission failure
    }

    auto& ticket_vector = tickets.value();
    // Validate upload tickets - if fewer tickets returned than requested, some
    // failed
    if (ticket_vector.size() != requests.size()) {
      LOG_F(ERROR,
        "Material upload submission failed: expected {} tickets, got {}. {} "
        "uploads failed.",
        requests.size(), ticket_vector.size(),
        requests.size() - ticket_vector.size());
    }
  }

  uploaded_this_frame_ = true;
}

auto MaterialBinder::GetMaterialsSrvIndex() const -> ShaderVisibleIndex
{
  DCHECK_NOTNULL_F(materials_atlas_.get(), "Atlas not initialized");
  if (materials_atlas_->GetBinding().srv == kInvalidShaderVisibleIndex) {
    if (const auto result = materials_atlas_->EnsureCapacity(
          std::max<std::uint32_t>(
            1U, static_cast<std::uint32_t>(materials_.size())),
          0.5f);
      !result) {
      LOG_F(ERROR,
        "Failed to ensure material atlas capacity for SRV creation: {}",
        result.error().message());
      return kInvalidShaderVisibleIndex;
    }
  }
  return materials_atlas_->GetBinding().srv;
}

} // namespace oxygen::renderer::resources
