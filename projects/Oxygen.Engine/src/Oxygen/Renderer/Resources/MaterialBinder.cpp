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
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

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
auto MakeMaterialKey(const oxygen::data::MaterialAsset& material) noexcept
  -> std::uint64_t
{
  // Hash based on key material properties for deduplication
  const auto base_color = material.GetBaseColor();
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
  const auto metalness_q = static_cast<std::uint32_t>(
                             std::round(material.GetMetalness() * scalar_scale))
    & 0xFFFF;
  const auto roughness_q = static_cast<std::uint32_t>(
                             std::round(material.GetRoughness() * scalar_scale))
    & 0xFFFF;

  hash ^= (static_cast<std::uint64_t>(metalness_q) << 32)
    | (static_cast<std::uint64_t>(roughness_q) << 48);

  // Hash texture indices
  hash ^= material.GetBaseColorTexture();
  hash ^= static_cast<std::uint64_t>(material.GetNormalTexture()) << 8;
  hash ^= static_cast<std::uint64_t>(material.GetMetallicTexture()) << 16;
  hash ^= static_cast<std::uint64_t>(material.GetRoughnessTexture()) << 24;
  hash ^= static_cast<std::uint64_t>(material.GetAmbientOcclusionTexture())
    << 32;

  // Hash flags
  hash ^= static_cast<std::uint64_t>(material.GetFlags()) << 40;

  return hash;
}

//! Serialize MaterialAsset data into MaterialConstants format.
auto SerializeMaterialConstants(
  const oxygen::data::MaterialAsset& material) noexcept
  -> oxygen::engine::MaterialConstants
{
  oxygen::engine::MaterialConstants constants;

  // Copy base color
  const auto base_color = material.GetBaseColor();
  constants.base_color
    = { base_color[0], base_color[1], base_color[2], base_color[3] };

  // Copy scalar properties
  constants.metalness = material.GetMetalness();
  constants.roughness = material.GetRoughness();
  constants.normal_scale = material.GetNormalScale();
  constants.ambient_occlusion = material.GetAmbientOcclusion();

  // Copy texture indices
  constants.base_color_texture_index = material.GetBaseColorTexture();
  constants.normal_texture_index = material.GetNormalTexture();
  constants.metallic_texture_index = material.GetMetallicTexture();
  constants.roughness_texture_index = material.GetRoughnessTexture();
  constants.ambient_occlusion_texture_index
    = material.GetAmbientOcclusionTexture();

  // Copy flags
  constants.flags = material.GetFlags();

  // Padding fields are already initialized to 0
  constants._pad0 = 0;
  constants._pad1 = 0;

  return constants;
}

} // namespace

namespace oxygen::renderer::resources {

MaterialBinder::MaterialBinder(
  Graphics& gfx, observer_ptr<engine::upload::UploadCoordinator> uploader)
  : gfx_(gfx)
  , uploader_(uploader)
{
}

MaterialBinder::~MaterialBinder()
{
  // Best-effort cleanup: unregister our GPU buffer from the registry so it
  // doesn't linger until registry destruction.
  auto& registry = gfx_.GetResourceRegistry();
  if (gpu_materials_buffer_) {
    registry.UnRegisterResource(*gpu_materials_buffer_);
  }
}

auto MaterialBinder::OnFrameStart() -> void
{
  // BeginFrame must be called once per frame by the orchestrator (Renderer).
  ++current_epoch_;
  if (current_epoch_ == 0U) {
    // Epoch overflow - reset all dirty tracking
    LOG_F(WARNING,
      "MaterialBinder::OnFrameStart - epoch overflow, resetting all dirty "
      "tracking");
    current_epoch_ = 1U;
    std::fill(dirty_epoch_.begin(), dirty_epoch_.end(), 0U);
  }
  dirty_indices_.clear();

  // Clear pending upload tickets from previous frame
  pending_upload_tickets_.clear();

  // Reset frame resource tracking
  frame_resources_ensured_ = false;
}

auto MaterialBinder::GetOrAllocate(
  std::shared_ptr<const data::MaterialAsset> material)
  -> engine::sceneprep::MaterialHandle
{
  LOG_F(2, "MaterialBinder::GetOrAllocate [this={}] called with material: '{}'",
    static_cast<void*>(this), material ? material->GetAssetName() : "null");

  // Handle null materials - return invalid handle that will fail IsValidHandle
  if (!material) {
    return engine::sceneprep::MaterialHandle {
      std::numeric_limits<std::uint32_t>::max()
    };
  }

  // Validate material
  std::string error_msg;
  if (!ValidateMaterial(*material, error_msg)) {
    LOG_F(ERROR, "Material validation failed: {}", error_msg);
    return engine::sceneprep::MaterialHandle {
      std::numeric_limits<std::uint32_t>::max()
    };
  }

  const auto key = MakeMaterialKey(*material);
  LOG_F(2, "MaterialBinder::GetOrAllocate material='{}' key=0x{:X}",
    material->GetAssetName(), key);

  if (const auto it = material_key_to_handle_.find(key);
    it != material_key_to_handle_.end()) {
    // Found existing handle - verify it's the same material to handle hash
    // collisions
    const auto handle = it->second;
    const auto idx = static_cast<std::size_t>(handle.get());
    if (idx < materials_.size() && materials_[idx].get() == material.get()) {
      // Mark dirty for this frame if not already dirty - this enables
      // auto-detection of material changes through overrides
      if (dirty_epoch_[idx] != current_epoch_) {
        dirty_epoch_[idx] = current_epoch_;
        dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
        LOG_F(1,
          "MaterialBinder::GetOrAllocate handle={} idx={} - marked existing "
          "material '{}' dirty for epoch {}",
          handle.get(), idx, material->GetAssetName(), current_epoch_);
      }
      return handle; // Same material, return existing handle
    }
    // Hash collision - need to allocate new handle
    LOG_F(2,
      "MaterialBinder::GetOrAllocate - hash collision for material '{}', "
      "allocating new handle",
      material->GetAssetName());
  }

  // Not found or collision mismatch: allocate new handle
  const auto handle = next_handle_;
  const auto idx = static_cast<std::size_t>(handle.get());

  // Resize vectors if needed
  if (materials_.size() <= idx) {
    const auto new_size = idx + 1U;
    materials_.resize(new_size);
    material_constants_.resize(new_size);
    versions_.resize(new_size);
    dirty_epoch_.resize(new_size);
  }

  ++global_version_;
  materials_[idx] = material;
  material_constants_[idx] = SerializeMaterialConstants(*material);
  versions_[idx] = global_version_;

  // Mark dirty for this frame
  if (dirty_epoch_[idx] != current_epoch_) {
    dirty_epoch_[idx] = current_epoch_;
    dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
  }

  material_key_to_handle_[key] = handle;
  next_handle_ = engine::sceneprep::MaterialHandle { handle.get() + 1U };

  return handle;
}

auto MaterialBinder::Update(engine::sceneprep::MaterialHandle handle,
  std::shared_ptr<const data::MaterialAsset> material) -> void
{
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
    LOG_F(2,
      "MaterialBinder::Update handle={} - same material pointer, skipping",
      handle.get());
    return; // Same material, no update needed
  }

  ++global_version_;
  materials_[idx] = material;
  const auto old_constants = material_constants_[idx];
  material_constants_[idx] = SerializeMaterialConstants(*material);
  versions_[idx] = global_version_;

  const auto& new_constants = material_constants_[idx];
  LOG_F(2,
    "MaterialBinder::Update handle={} idx={} - "
    "base_color=[{:.3f},{:.3f},{:.3f},{:.3f}] -> [{:.3f},{:.3f},{:.3f},{:.3f}]",
    handle.get(), idx, old_constants.base_color[0], old_constants.base_color[1],
    old_constants.base_color[2], old_constants.base_color[3],
    new_constants.base_color[0], new_constants.base_color[1],
    new_constants.base_color[2], new_constants.base_color[3]);

  // Mark dirty for this frame
  if (dirty_epoch_[idx] != current_epoch_) {
    dirty_epoch_[idx] = current_epoch_;
    dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
    LOG_F(2,
      "MaterialBinder::Update handle={} idx={} - marked dirty for epoch {}, "
      "dirty_indices size={}",
      handle.get(), idx, current_epoch_, dirty_indices_.size());
  } else {
    LOG_F(2,
      "MaterialBinder::Update handle={} idx={} - already dirty for epoch {}",
      handle.get(), idx, current_epoch_);
  }
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

auto MaterialBinder::EnsureBufferAndSrv(
  std::shared_ptr<graphics::Buffer>& buffer, ShaderVisibleIndex& bindless_index,
  std::uint64_t size_bytes, const char* debug_label) -> bool
{
  // Check if buffer needs to be created or resized
  bool needs_new_buffer = false;
  if (!buffer || buffer->GetDescriptor().size_bytes < size_bytes) {
    needs_new_buffer = true;
  }

  if (needs_new_buffer) {
    auto& registry = gfx_.GetResourceRegistry();
    auto& allocator = gfx_.GetDescriptorAllocator();

    // Unregister old buffer if it exists
    if (buffer) {
      registry.UnRegisterResource(*buffer);
    }

    // Create new buffer
    graphics::BufferDesc desc;
    desc.size_bytes = size_bytes;
    desc.usage = graphics::BufferUsage::kStorage;
    desc.memory = graphics::BufferMemory::kDeviceLocal;
    desc.debug_name = debug_label;

    buffer = gfx_.CreateBuffer(desc);

    // Register buffer and create SRV
    registry.Register(buffer);

    graphics::BufferViewDescription view_desc;
    view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
    view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    view_desc.format = Format::kUnknown;
    view_desc.range = { 0, size_bytes };
    view_desc.stride = sizeof(engine::MaterialConstants);

    auto handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    bindless_index = allocator.GetShaderVisibleIndex(handle);

    const auto view = buffer->GetNativeView(handle, view_desc);
    registry.RegisterView(*buffer, view, std::move(handle), view_desc);

    return true; // Buffer was created/resized
  }

  return false; // Buffer already exists and is sufficient
}

auto MaterialBinder::BuildSparseUploadRequests(
  const std::vector<std::uint32_t>& indices,
  std::span<const engine::MaterialConstants> src,
  const std::shared_ptr<graphics::Buffer>& dst,
  const char* /*debug_name*/) const
  -> std::vector<engine::upload::UploadRequest>
{
  std::vector<engine::upload::UploadRequest> requests;
  requests.reserve(indices.size());

  constexpr std::uint64_t kMaterialConstantsSize
    = sizeof(engine::MaterialConstants);

  for (const auto idx : indices) {
    if (idx >= src.size()) {
      LOG_F(WARNING, "Skipping out-of-bounds material index {} (size={})", idx,
        src.size());
      continue;
    }

    requests.emplace_back(engine::upload::UploadRequest {
      .kind = engine::upload::UploadKind::kBuffer,
      .desc = engine::upload::UploadBufferDesc { dst, kMaterialConstantsSize,
        idx * kMaterialConstantsSize },
      .data = engine::upload::UploadDataView { std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(&src[idx]),
        kMaterialConstantsSize) } });
  }

  return requests;
}

auto MaterialBinder::PrepareMaterialConstants() -> void
{
  if (material_constants_.empty()) {
    LOG_F(
      2, "MaterialBinder::PrepareMaterialConstants - no materials, skipping");
    return; // Nothing to upload
  }

  LOG_F(2,
    "MaterialBinder::PrepareMaterialConstants - {} materials, {} dirty indices",
    material_constants_.size(), dirty_indices_.size());

  // Calculate required buffer size
  constexpr std::uint64_t kMaterialConstantsSize
    = sizeof(engine::MaterialConstants);
  const auto buffer_size = material_constants_.size() * kMaterialConstantsSize;

  // Ensure buffer and SRV exist
  const bool buffer_changed = EnsureBufferAndSrv(gpu_materials_buffer_,
    materials_bindless_index_, buffer_size, "MaterialConstantsBuffer");

  // Upload all materials if buffer changed, otherwise only dirty materials
  if (buffer_changed) {
    LOG_F(1,
      "MaterialBinder::PrepareMaterialConstants - buffer changed, uploading "
      "all {} materials",
      material_constants_.size());
    // Upload entire buffer
    std::vector<std::byte> upload_data(buffer_size);
    std::memcpy(upload_data.data(), material_constants_.data(), buffer_size);

    engine::upload::UploadRequest req;
    req.kind = engine::upload::UploadKind::kBuffer;
    req.desc = engine::upload::UploadBufferDesc { gpu_materials_buffer_,
      buffer_size, 0 };
    req.data = engine::upload::UploadDataView { std::span<const std::byte>(
      upload_data.data(), buffer_size) };
    auto ticket = uploader_->Submit(req);
    pending_upload_tickets_.push_back(ticket);
  } else if (!dirty_indices_.empty()) {
    LOG_F(1,
      "MaterialBinder::PrepareMaterialConstants - uploading {} dirty materials",
      dirty_indices_.size());

    // Upload only dirty materials
    auto upload_requests = BuildSparseUploadRequests(dirty_indices_,
      std::span<const engine::MaterialConstants>(
        material_constants_.data(), material_constants_.size()),
      gpu_materials_buffer_, "MaterialConstants");

    for (auto& req : upload_requests) {
      auto ticket = uploader_->Submit(req);
      pending_upload_tickets_.push_back(ticket);
    }
  } else {
    LOG_F(2,
      "MaterialBinder::PrepareMaterialConstants - no dirty materials, no "
      "upload needed");
  }
}

auto MaterialBinder::EnsureFrameResources() -> void
{
  if (frame_resources_ensured_) {
    return; // Already prepared for this frame
  }

  PrepareMaterialConstants();

  frame_resources_ensured_ = true;
}

auto MaterialBinder::GetMaterialsSrvIndex() const -> ShaderVisibleIndex
{
  DCHECK_F(frame_resources_ensured_,
    "GetMaterialsSrvIndex called before EnsureFrameResources()");
  DCHECK_F(materials_bindless_index_ != kInvalidShaderVisibleIndex,
    "Materials SRV index is invalid - ensure materials were allocated");
  return materials_bindless_index_;
}

} // namespace oxygen::renderer::resources
