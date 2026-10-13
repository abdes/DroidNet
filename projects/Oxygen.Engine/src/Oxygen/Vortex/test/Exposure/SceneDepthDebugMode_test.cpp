//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/test/Exposure/Fixtures/ExposureLightingFixture.h>

namespace oxygen::vortex::testing::exposure {

//! The scene-depth debug modes read depth from the view's scene-texture
//! record; a surface shows its depth as gray, never the magenta error color.
NOLINT_TEST_F(ExposureLightingGpuTest, SceneDepthDebugModesRenderDepth)
{
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  SetSurface(data::MaterialDomain::kOpaque);
  for (const auto mode : {
         ShaderDebugMode::kSceneDepthRaw,
         ShaderDebugMode::kSceneDepthLinear,
       }) {
    SCOPED_TRACE(to_string(mode));
    renderer_->SetShaderDebugMode(mode);
    RenderSurface(false, 0.0F, 3U);
    ASSERT_FALSE(HasFatalFailure());
    // Debug modes write the view's output directly.
    const auto pixel = ReadFloatTexture(
      *framebuffer->GetDescriptor().color_attachments.front().texture)
                         .at(0);
    EXPECT_NEAR(pixel.at(0), pixel.at(1), 1.0e-5F);
    EXPECT_NEAR(pixel.at(1), pixel.at(2), 1.0e-5F);
    EXPECT_GT(pixel.at(0), 0.0F);
    EXPECT_LT(pixel.at(0), 1.0F);
  }
  renderer_->SetShaderDebugMode(ShaderDebugMode::kDisabled);
}

} // namespace oxygen::vortex::testing::exposure
