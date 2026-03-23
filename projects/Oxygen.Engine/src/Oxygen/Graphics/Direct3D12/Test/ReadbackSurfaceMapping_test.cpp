//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

namespace {

using oxygen::TextureType;
using oxygen::graphics::ReadbackError;
using oxygen::graphics::ResourceAccessMode;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureSlice;
using oxygen::graphics::d3d12::testing::ReadbackTestFixture;

class ReadbackSurfaceTestBase : public ReadbackTestFixture {
protected:
  auto CreateReadbackSurface(TextureDesc desc)
    -> std::shared_ptr<oxygen::graphics::Texture>
  {
    auto surface = GetReadbackManager()->CreateReadbackTextureSurface(desc);
    CHECK_F(surface.has_value(), "Failed to create readback surface: {}",
      surface.error());
    CHECK_NOTNULL_F(surface->get());
    return *surface;
  }
};

class ReadbackSurfaceCreationTest : public ReadbackSurfaceTestBase { };
class ReadbackSurfaceMappingTest : public ReadbackSurfaceTestBase { };

NOLINT_TEST_F(ReadbackSurfaceCreationTest,
  CreateReadbackTextureSurfaceNormalizesDescriptorForReadbackUse)
{
  TextureDesc desc {};
  desc.width = 8;
  desc.height = 4;
  desc.sample_count = 4;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2DMultiSample;
  desc.is_shader_resource = true;
  desc.is_render_target = true;
  desc.is_uav = true;
  desc.debug_name = "surface-create";

  auto surface = CreateReadbackSurface(desc);
  ASSERT_EQ(
    surface->GetTypeId(), oxygen::graphics::d3d12::Texture::ClassTypeId());
  auto* d3d12_surface
    = static_cast<oxygen::graphics::d3d12::Texture*>(surface.get());

  const auto& surface_desc = d3d12_surface->GetDescriptor();
  EXPECT_EQ(surface_desc.cpu_access, ResourceAccessMode::kReadBack);
  EXPECT_EQ(surface_desc.sample_count, 1U);
  EXPECT_EQ(surface_desc.texture_type, TextureType::kTexture2D);
  EXPECT_FALSE(surface_desc.is_shader_resource);
  EXPECT_FALSE(surface_desc.is_render_target);
  EXPECT_FALSE(surface_desc.is_uav);
  EXPECT_TRUE(d3d12_surface->IsReadbackSurface());
}

NOLINT_TEST_F(ReadbackSurfaceCreationTest,
  CreateReadbackTextureSurfaceRejectsUnsupportedDepthStencilFormat)
{
  TextureDesc desc {};
  desc.width = 4;
  desc.height = 4;
  desc.format = oxygen::Format::kDepth24Stencil8;
  desc.texture_type = TextureType::kTexture2D;
  desc.debug_name = "surface-depth";

  const auto surface = GetReadbackManager()->CreateReadbackTextureSurface(desc);
  ASSERT_FALSE(surface.has_value());
  EXPECT_EQ(surface.error(), ReadbackError::kUnsupportedResource);
}

NOLINT_TEST_F(ReadbackSurfaceMappingTest,
  MapReadbackTextureSurfaceReturnsExactSubresourcePitchMetadata)
{
  TextureDesc desc {};
  desc.width = 8;
  desc.height = 4;
  desc.array_size = 2;
  desc.mip_levels = 2;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2DArray;
  desc.debug_name = "surface-map";

  auto surface = CreateReadbackSurface(desc);
  ASSERT_EQ(
    surface->GetTypeId(), oxygen::graphics::d3d12::Texture::ClassTypeId());
  auto* d3d12_surface
    = static_cast<oxygen::graphics::d3d12::Texture*>(surface.get());

  const TextureSlice slice {
    .mip_level = 1,
    .array_slice = 1,
  };
  const auto mapping
    = GetReadbackManager()->MapReadbackTextureSurface(*surface, slice);
  ASSERT_TRUE(mapping.has_value());
  ASSERT_NE(mapping->data, nullptr);

  const auto resolved = slice.Resolve(d3d12_surface->GetDescriptor());
  const auto subresource_index = resolved.mip_level
    + (resolved.array_slice * d3d12_surface->GetDescriptor().mip_levels);
  const auto& subresource_layout
    = d3d12_surface->GetReadbackSurfaceLayout().subresources[subresource_index];

  EXPECT_EQ(mapping->layout.format, oxygen::Format::kRGBA8UNorm);
  EXPECT_EQ(mapping->layout.texture_type, TextureType::kTexture2DArray);
  EXPECT_EQ(mapping->layout.width, resolved.width);
  EXPECT_EQ(mapping->layout.height, resolved.height);
  EXPECT_EQ(mapping->layout.depth, resolved.depth);
  EXPECT_EQ(mapping->layout.row_pitch.get(),
    subresource_layout.placed_footprint.Footprint.RowPitch);
  EXPECT_EQ(mapping->layout.slice_pitch.get(),
    subresource_layout.placed_footprint.Footprint.RowPitch
      * static_cast<uint64_t>(subresource_layout.row_count));
  EXPECT_EQ(mapping->layout.mip_level, resolved.mip_level);
  EXPECT_EQ(mapping->layout.array_slice, resolved.array_slice);

  GetReadbackManager()->UnmapReadbackTextureSurface(*surface);
}

NOLINT_TEST_F(ReadbackSurfaceMappingTest,
  MapReadbackTextureSurfaceRejectsNonReadbackTexture)
{
  TextureDesc desc {};
  desc.width = 4;
  desc.height = 4;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2D;
  desc.debug_name = "non-readback-surface";

  auto texture = CreateTexture(desc);
  ASSERT_NE(texture, nullptr);

  const auto mapping
    = GetReadbackManager()->MapReadbackTextureSurface(*texture, {});
  ASSERT_FALSE(mapping.has_value());
  EXPECT_EQ(mapping.error(), ReadbackError::kUnsupportedResource);
}

NOLINT_TEST_F(ReadbackSurfaceMappingTest,
  MapReadbackTextureSurfaceRejectsSecondMapUntilUnmapped)
{
  TextureDesc desc {};
  desc.width = 4;
  desc.height = 4;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2D;
  desc.debug_name = "surface-double-map";

  auto surface = CreateReadbackSurface(desc);

  const auto first
    = GetReadbackManager()->MapReadbackTextureSurface(*surface, {});
  ASSERT_TRUE(first.has_value());

  const auto second
    = GetReadbackManager()->MapReadbackTextureSurface(*surface, {});
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ReadbackError::kAlreadyMapped);

  GetReadbackManager()->UnmapReadbackTextureSurface(*surface);

  const auto third
    = GetReadbackManager()->MapReadbackTextureSurface(*surface, {});
  ASSERT_TRUE(third.has_value());
  GetReadbackManager()->UnmapReadbackTextureSurface(*surface);
}

} // namespace
