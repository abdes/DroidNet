//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/Unorm16.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>

namespace oxygen::vortex::testing::exposure {

namespace {

  //! An opaque white surface, optionally with a procedural grid whose minor
  //! lines cover every pixel in pure red.
  auto MakeGridSurfaceMaterial(const bool with_grid, const std::string& name)
    -> std::shared_ptr<data::MaterialAsset>
  {
    auto desc = data::pak::render::MaterialAssetDesc {};
    desc.material_domain
      = static_cast<std::uint8_t>(data::MaterialDomain::kOpaque);
    desc.flags = data::pak::render::kMaterialFlag_NoTextureSampling
      | data::pak::render::kMaterialFlag_DoubleSided;
    if (with_grid) {
      desc.flags |= data::pak::render::kMaterialFlag_ProceduralGrid;
    }
    desc.base_color[0] = desc.base_color[1] = desc.base_color[2] = 1.0F;
    desc.base_color[3] = 1.0F;
    desc.normal_scale = 1.0F;
    desc.roughness = data::Unorm16 { 1.0F };
    desc.ambient_occlusion = data::Unorm16 { 1.0F };
    desc.uv_scale[0] = desc.uv_scale[1] = 1.0F;
    desc.grid_spacing[0] = desc.grid_spacing[1] = 1.0F;
    desc.grid_major_every = 1U;
    // Line widths as wide as the spacing cover the whole surface.
    desc.grid_line_thickness = 1.0F;
    desc.grid_major_thickness = 1.0F;
    desc.grid_fade_start = 1.0e6F;
    desc.grid_fade_end = 2.0e6F;
    desc.grid_minor_color[0] = 1.0F;
    desc.grid_minor_color[3] = 1.0F;
    desc.grid_major_color[0] = 1.0F;
    desc.grid_major_color[3] = 1.0F;
    return std::make_shared<data::MaterialAsset>(
      data::AssetKey::FromVirtualPath("/Test/Exposure/" + name + ".omat"), desc,
      std::vector<data::ShaderReference> {});
  }

} // namespace

//! A procedural-grid material reaches its grid constants through the view's
//! draw-frame record, so the grid colors the surface in both shading paths.
NOLINT_TEST_F(ExposureLightingGpuTest, ProceduralGridMaterialColorsTheSurface)
{
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  for (const bool forward : { false, true }) {
    SCOPED_TRACE(forward);
    const auto render = [&](const bool with_grid) -> Pixel {
      mesh_node.GetRenderable().SetMaterialOverride(data::LodIndex { 0 },
        data::SubmeshIndex { 0 },
        MakeGridSurfaceMaterial(with_grid,
          std::string(with_grid ? "Grid" : "Plain")
            + (forward ? "Forward" : "Deferred")));
      RenderSurface(forward, 0.0F, 3U);
      return ReadFloatTexture(*probe->color).at(0);
    };
    const auto plain = render(false);
    ASSERT_FALSE(HasFatalFailure());
    const auto grid = render(true);
    ASSERT_FALSE(HasFatalFailure());
    // Without the grid constants the flagged material renders exactly like
    // the plain one; with them, the grid's red lines brighten the surface.
    EXPECT_NEAR(plain.at(0), plain.at(1), 1.0e-4F * (1.0F + plain.at(0)));
    EXPECT_GT(grid.at(0), plain.at(0) + 0.25F);
  }
}

} // namespace oxygen::vortex::testing::exposure
