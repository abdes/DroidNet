//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/data/MaterialAsset.h>

namespace oxygen::renderer::resources {

MaterialBinder::MaterialBinder(std::weak_ptr<Graphics> graphics,
  observer_ptr<engine::upload::UploadCoordinator> uploader,
  const data::MaterialAsset* default_material)
  : graphics_(std::move(graphics))
  , uploader_(uploader)
  , default_material_(default_material)
{
}

MaterialBinder::~MaterialBinder() = default;

auto MaterialBinder::ProcessMaterials(
  const std::vector<const data::MaterialAsset*>& materials)
  -> MaterialConstantsBufferInfo
{
  // Deduplicate materials and assign slots, guarantee slot 0 is default
  MaterialConstantsBufferInfo info;
  std::unordered_map<const data::MaterialAsset*, uint32_t> material_to_slot;
  std::vector<const data::MaterialAsset*> unique_materials;

  // Slot 0: always default material
  if (default_material_) {
    material_to_slot[default_material_] = 0;
    unique_materials.push_back(default_material_);
  }

  for (const auto* mat : materials) {
    if (!mat) {
      continue;
    }
    if (!material_to_slot.contains(mat)) {
      uint32_t slot = static_cast<uint32_t>(unique_materials.size());
      material_to_slot[mat] = slot;
      unique_materials.push_back(mat);
    }
  }

  // Map input material index to slot
  for (size_t i = 0; i < materials.size(); ++i) {
    const auto* mat = materials[i];
    if (!mat) {
      continue;
    }
    info.handle_to_slot[static_cast<uint32_t>(i)] = material_to_slot[mat];
  }

  auto graphics = graphics_.lock();
  if (!graphics) {
    return info;
  }
  auto& allocator = graphics->GetDescriptorAllocator();
  auto& registry = graphics->GetResourceRegistry();

  // Use real MaterialConstants size
  constexpr size_t kMaterialConstantsSize = sizeof(engine::MaterialConstants);
  const uint64_t buffer_size = unique_materials.size() * kMaterialConstantsSize;
  graphics::BufferDesc desc;
  desc.size_bytes = buffer_size;
  desc.usage = graphics::BufferUsage::kConstant;
  desc.memory = graphics::BufferMemory::kDeviceLocal;
  desc.debug_name = "MaterialConstantsBuffer";

  // Create buffer
  info.buffer = graphics->CreateBuffer(desc);

  // Prepare upload data (serialize each material's constants)
  std::vector<std::byte> upload_data(buffer_size);
  for (size_t i = 0; i < unique_materials.size(); ++i) {
    const auto* mat = unique_materials[i];
    engine::MaterialConstants constants;
    if (mat) {
      auto base_color = mat->GetBaseColor();
      std::memcpy(
        &constants.base_color, base_color.data(), sizeof(constants.base_color));
      constants.metalness = mat->GetMetalness();
      constants.roughness = mat->GetRoughness();
      constants.normal_scale = mat->GetNormalScale();
      constants.ambient_occlusion = mat->GetAmbientOcclusion();
      constants.base_color_texture_index = mat->GetBaseColorTexture();
      constants.normal_texture_index = mat->GetNormalTexture();
      constants.metallic_texture_index = mat->GetMetallicTexture();
      constants.roughness_texture_index = mat->GetRoughnessTexture();
      constants.ambient_occlusion_texture_index
        = mat->GetAmbientOcclusionTexture();
      constants.flags = mat->GetFlags();
      constants._pad0 = 0;
      constants._pad1 = 0;
    } else {
      constants = {};
    }
    std::memcpy(upload_data.data() + i * kMaterialConstantsSize, &constants,
      kMaterialConstantsSize);
  }

  // Schedule upload
  engine::upload::UploadRequest req;
  req.kind = engine::upload::UploadKind::kBuffer;
  req.desc = engine::upload::UploadBufferDesc { info.buffer, buffer_size, 0 };
  req.data = engine::upload::UploadDataView { std::span<const std::byte>(
    upload_data.data(), buffer_size) };
  uploader_->Submit(req);

  // Register buffer and SRV
  registry.Register(info.buffer);
  graphics::BufferViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  view_desc.range = { 0, buffer_size };
  auto handle = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
    graphics::DescriptorVisibility::kShaderVisible);
  info.bindless_index = allocator.GetShaderVisibleIndex(handle).get();
  registry.RegisterView(*info.buffer, std::move(handle), view_desc);

  return info;
}

} // namespace oxygen::renderer::resources
