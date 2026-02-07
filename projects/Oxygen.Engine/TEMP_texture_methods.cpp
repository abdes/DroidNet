//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// TEMPORARY FILE - Contains the three new texture creation methods
// These need to be inserted into SkyAtmosphereLutManager.cpp at line 251

// Common implementation for creating LUT textures
auto SkyAtmosphereLutManager::CreateLutTextureImpl(uint32_t width,
  uint32_t height, uint32_t depth_or_array_size, bool is_rgba,
  const char* debug_name, TextureType texture_type)
  -> std::shared_ptr<graphics::Texture>
{
  TextureDesc desc;
  desc.width = width;
  desc.height = height;
  desc.mip_levels = 1u;
  desc.sample_count = 1u;
  desc.format = is_rgba ? Format::kRGBA16Float : Format::kRG16Float;
  desc.debug_name = debug_name;
  desc.is_shader_resource = true;
  desc.is_uav = true;
  desc.is_render_target = false;
  desc.initial_state = graphics::ResourceStates::kUnorderedAccess;
  desc.texture_type = texture_type;

  // Set depth or array_size based on texture type
  if (texture_type == TextureType::kTexture3D) {
    desc.depth = depth_or_array_size;
  } else if (texture_type == TextureType::kTexture2DArray) {
    desc.array_size = depth_or_array_size;
  }
  // For Texture2D, depth_or_array_size is ignored

  auto texture = gfx_->CreateTexture(desc);
  if (!texture) {
    LOG_F(ERROR, "SkyAtmosphereLutManager: failed to create texture '{}'",
      debug_name);
    return nullptr;
  }

  texture->SetName(desc.debug_name);
  gfx_->GetResourceRegistry().Register(texture);

  return texture;
}

// Creates a 2D array LUT texture (e.g., sky-view with altitude slices)
auto SkyAtmosphereLutManager::Create2DArrayLutTexture(uint32_t width,
  uint32_t height, uint32_t array_size, bool is_rgba, const char* debug_name)
  -> std::shared_ptr<graphics::Texture>
{
  const auto texture_type
    = (array_size > 1) ? TextureType::kTexture2DArray : TextureType::kTexture2D;
  return CreateLutTextureImpl(
    width, height, array_size, is_rgba, debug_name, texture_type);
}

// Creates a 3D LUT texture (e.g., camera volume froxels)
auto SkyAtmosphereLutManager::Create3DLutTexture(uint32_t width,
  uint32_t height, uint32_t depth, bool is_rgba, const char* debug_name)
  -> std::shared_ptr<graphics::Texture>
{
  return CreateLutTextureImpl(
    width, height, depth, is_rgba, debug_name, TextureType::kTexture3D);
}
