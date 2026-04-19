//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include <glm/vec2.hpp>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>

namespace {

using oxygen::Format;
using oxygen::vortex::GBufferIndex;
using oxygen::vortex::SceneTextureAspectView;
using oxygen::vortex::SceneTextureBindings;
using oxygen::vortex::SceneTextureSetupMode;
using oxygen::vortex::SceneTextures;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::ViewFrameBindings;
using oxygen::vortex::testing::FakeGraphics;

auto MakeConfig() -> SceneTexturesConfig
{
  return SceneTexturesConfig {
    .extent = { 160, 90 },
    .enable_velocity = true,
    .enable_custom_depth = false,
    .gbuffer_count = 4,
    .msaa_sample_count = 1,
  };
}

void ExpectTexture(const oxygen::graphics::Texture& texture,
  const Format expected_format, const glm::uvec2 expected_extent,
  const char* expected_name)
{
  const auto& desc = texture.GetDescriptor();
  EXPECT_EQ(desc.format, expected_format);
  EXPECT_EQ(desc.width, expected_extent.x);
  EXPECT_EQ(desc.height, expected_extent.y);
  EXPECT_EQ(desc.debug_name, expected_name);
}

TEST(SceneTexturesContractTest, AllocatesTheExactPhase2Subset)
{
  FakeGraphics graphics;
  const auto config = MakeConfig();

  SceneTextures scene_textures(graphics, config);

  EXPECT_EQ(scene_textures.GetConfig().gbuffer_count, 4U);
  EXPECT_EQ(scene_textures.GetExtent().x, config.extent.x);
  EXPECT_EQ(scene_textures.GetExtent().y, config.extent.y);

  ExpectTexture(
    scene_textures.GetSceneColor(), Format::kRGBA16Float, config.extent, "SceneColor");
  ExpectTexture(
    scene_textures.GetSceneDepth(), Format::kDepth32Stencil8, config.extent, "SceneDepth");
  EXPECT_TRUE(scene_textures.GetSceneDepth().GetDescriptor().is_render_target);
  ExpectTexture(scene_textures.GetPartialDepth(), Format::kR32Float,
    config.extent, "PartialDepth");
  ExpectTexture(scene_textures.GetGBufferNormal(), Format::kR10G10B10A2UNorm,
    config.extent, "GBufferNormal");
  ExpectTexture(scene_textures.GetGBufferMaterial(), Format::kRGBA8UNorm,
    config.extent, "GBufferMaterial");
  ExpectTexture(scene_textures.GetGBufferBaseColor(), Format::kRGBA8UNormSRGB,
    config.extent, "GBufferBaseColor");
  ExpectTexture(scene_textures.GetGBufferCustomData(), Format::kRGBA8UNorm,
    config.extent, "GBufferCustomData");

  ASSERT_NE(scene_textures.GetVelocity(), nullptr);
  ExpectTexture(*scene_textures.GetVelocity(), Format::kRG16Float, config.extent,
    "Velocity");
  EXPECT_TRUE(scene_textures.GetVelocity()->GetDescriptor().is_uav);
  EXPECT_EQ(scene_textures.GetCustomDepth(), nullptr);

  const auto stencil = scene_textures.GetStencil();
  EXPECT_TRUE(stencil.IsValid());
  EXPECT_EQ(stencil.texture, &scene_textures.GetSceneDepth());
  EXPECT_EQ(stencil.aspect, SceneTextureAspectView::Aspect::kStencil);

  const auto custom_stencil = scene_textures.GetCustomStencil();
  EXPECT_FALSE(custom_stencil.IsValid());
  EXPECT_EQ(custom_stencil.texture, nullptr);
  EXPECT_EQ(custom_stencil.aspect, SceneTextureAspectView::Aspect::kStencil);

  EXPECT_DEATH_IF_SUPPORTED(
    static_cast<void>(scene_textures.GetGBuffer(GBufferIndex::kShadowFactors)),
    "GBufferShadowFactors");
  EXPECT_DEATH_IF_SUPPORTED(
    static_cast<void>(scene_textures.GetGBuffer(GBufferIndex::kWorldTangent)),
    "GBufferWorldTangent");
}

TEST(SceneTexturesContractTest, ResizeReallocatesTheProductFamily)
{
  FakeGraphics graphics;
  SceneTextures scene_textures(graphics, MakeConfig());

  auto* old_scene_color = &scene_textures.GetSceneColor();

  scene_textures.Resize({ 320, 180 });

  EXPECT_EQ(scene_textures.GetExtent().x, 320U);
  EXPECT_EQ(scene_textures.GetExtent().y, 180U);
  EXPECT_NE(&scene_textures.GetSceneColor(), old_scene_color);
  ExpectTexture(scene_textures.GetGBufferNormal(), Format::kR10G10B10A2UNorm,
    glm::uvec2 { 320U, 180U }, "GBufferNormal");
}

TEST(SceneTexturesContractTest, RejectsInvalidConfig)
{
  FakeGraphics graphics;

  auto zero_extent = MakeConfig();
  zero_extent.extent = { 0, 90 };
  EXPECT_THROW(SceneTextures unused(graphics, zero_extent), std::invalid_argument);

  auto zero_height = MakeConfig();
  zero_height.extent = { 160, 0 };
  EXPECT_THROW(SceneTextures unused(graphics, zero_height), std::invalid_argument);

  auto wrong_gbuffer_count = MakeConfig();
  wrong_gbuffer_count.gbuffer_count = 3;
  EXPECT_THROW(
    SceneTextures unused(graphics, wrong_gbuffer_count), std::invalid_argument);

  wrong_gbuffer_count.gbuffer_count = 5;
  EXPECT_THROW(
    SceneTextures unused_again(graphics, wrong_gbuffer_count), std::invalid_argument);
}

TEST(SceneTexturesContractTest,
  CustomDepthAndStencilAccessorsFollowTheConfiguredProductFamily)
{
  FakeGraphics graphics;
  auto config = MakeConfig();
  config.enable_velocity = false;
  config.enable_custom_depth = true;

  SceneTextures scene_textures(graphics, config);
  auto* gbuffer_normal_before = &scene_textures.GetGBufferNormal();

  EXPECT_EQ(scene_textures.GetVelocity(), nullptr);
  ASSERT_NE(scene_textures.GetCustomDepth(), nullptr);
  EXPECT_TRUE(scene_textures.GetCustomDepth()->GetDescriptor().is_render_target);

  const auto custom_stencil = scene_textures.GetCustomStencil();
  EXPECT_TRUE(custom_stencil.IsValid());
  EXPECT_EQ(custom_stencil.texture, scene_textures.GetCustomDepth());
  EXPECT_EQ(custom_stencil.aspect, SceneTextureAspectView::Aspect::kStencil);

  scene_textures.RebuildWithGBuffers();

  EXPECT_EQ(&scene_textures.GetGBufferNormal(), gbuffer_normal_before);
}

TEST(SceneTextureSetupModeContractTest, FlagsComposeWithoutAdHocSequencing)
{
  auto setup_mode = SceneTextureSetupMode {};

  setup_mode.SetFlags(SceneTextureSetupMode::Flag::kSceneDepth
    | SceneTextureSetupMode::Flag::kPartialDepth
    | SceneTextureSetupMode::Flag::kSceneVelocity);

  EXPECT_TRUE(setup_mode.IsSet(SceneTextureSetupMode::Flag::kSceneDepth));
  EXPECT_TRUE(setup_mode.IsSet(SceneTextureSetupMode::Flag::kPartialDepth));
  EXPECT_TRUE(setup_mode.IsSet(SceneTextureSetupMode::Flag::kSceneVelocity));

  setup_mode.Clear(SceneTextureSetupMode::Flag::kSceneVelocity);
  EXPECT_FALSE(setup_mode.IsSet(SceneTextureSetupMode::Flag::kSceneVelocity));
}

auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << path.string();
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

TEST(SceneTextureBindingsContractTest, ReservesFutureGBufferSlotsInThePublishedAbi)
{
  SceneTextureBindings bindings {};

  EXPECT_EQ(bindings.gbuffer_srvs.size(),
    static_cast<std::size_t>(GBufferIndex::kCount));
  EXPECT_EQ(bindings.gbuffer_srvs[static_cast<std::size_t>(GBufferIndex::kNormal)],
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[static_cast<std::size_t>(GBufferIndex::kMaterial)],
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[static_cast<std::size_t>(GBufferIndex::kBaseColor)],
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[static_cast<std::size_t>(GBufferIndex::kCustomData)],
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[static_cast<std::size_t>(GBufferIndex::kShadowFactors)],
    SceneTextureBindings::kInvalidIndex);
  EXPECT_EQ(bindings.gbuffer_srvs[static_cast<std::size_t>(GBufferIndex::kWorldTangent)],
    SceneTextureBindings::kInvalidIndex);
}

TEST(ViewFrameBindingsContractTest, SceneTextureSlotPreservesLayoutContract)
{
  ViewFrameBindings bindings {};
  const auto source_root
    = std::filesystem::path { __FILE__ }.parent_path().parent_path().parent_path();
  const auto hlsl_path = source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Contracts/ViewFrameBindings.hlsli";
  const auto hlsl_source = ReadTextFile(hlsl_path);

  EXPECT_EQ(bindings.scene_texture_frame_slot, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(sizeof(ViewFrameBindings), 64U);
  EXPECT_EQ(alignof(ViewFrameBindings), 16U);
  EXPECT_TRUE(hlsl_source.contains("uint _pad0;"));
  EXPECT_TRUE(hlsl_source.contains("uint _pad1;"));
  EXPECT_TRUE(hlsl_source.contains("uint _pad2;"));
}

} // namespace
