//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>

#include "DepthPrePassGpuTestFixture.h"

namespace {

using oxygen::NdcDepthRange;
using oxygen::Scissors;
using oxygen::ViewPort;
using oxygen::engine::DepthPrePass;
using oxygen::engine::DepthPrePassOutput;
using oxygen::engine::testing::DepthPrePassGpuTestFixture;

// -- Fixture for config-level tests (no geometry, no GPU execution) ----------

class DepthPrePassConfigTest : public DepthPrePassGpuTestFixture { };

NOLINT_TEST_F(DepthPrePassConfigTest, OutputDefaultsToFullTextureRectWhenUnset)
{
  auto depth_texture = CreateDepthTexture(8U, 6U, "config.output.full");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "config.output.full",
    }));

  const auto output = pass.GetOutput();
  ASSERT_NE(output.depth_texture, nullptr);
  EXPECT_EQ(output.depth_texture, depth_texture.get());
  EXPECT_FALSE(output.canonical_srv_index.IsValid());
  EXPECT_EQ(output.width, 8U);
  EXPECT_EQ(output.height, 6U);
  EXPECT_FLOAT_EQ(output.viewport.top_left_x, 0.0F);
  EXPECT_FLOAT_EQ(output.viewport.top_left_y, 0.0F);
  EXPECT_FLOAT_EQ(output.viewport.width, 8.0F);
  EXPECT_FLOAT_EQ(output.viewport.height, 6.0F);
  EXPECT_EQ(output.scissors.left, 0);
  EXPECT_EQ(output.scissors.top, 0);
  EXPECT_EQ(output.scissors.right, 8);
  EXPECT_EQ(output.scissors.bottom, 6);
  EXPECT_EQ(output.valid_rect.left, 0);
  EXPECT_EQ(output.valid_rect.top, 0);
  EXPECT_EQ(output.valid_rect.right, 8);
  EXPECT_EQ(output.valid_rect.bottom, 6);
  EXPECT_EQ(output.ndc_depth_range, NdcDepthRange::ZeroToOne);
  EXPECT_TRUE(output.reverse_z);
  EXPECT_TRUE(output.has_depth_texture);
  EXPECT_FALSE(output.has_canonical_srv);
  EXPECT_FALSE(output.is_complete);
}

NOLINT_TEST_F(
  DepthPrePassConfigTest, RejectsViewportScissorsPairsThatProduceEmptyDepthRect)
{
  auto depth_texture = CreateDepthTexture(4U, 4U, "config.empty-rect");
  ASSERT_NE(depth_texture, nullptr);

  auto make_pass = [&]() {
    return DepthPrePass(
      std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
        .depth_texture = depth_texture,
        .debug_name = "config.empty-rect",
      }));
  };

  {
    auto pass = make_pass();
    pass.SetViewport(ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 2.0F,
      .height = 4.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    });
    EXPECT_THROW(pass.SetScissors(
                   Scissors { .left = 2, .top = 0, .right = 4, .bottom = 4 }),
      std::invalid_argument);
  }

  {
    auto pass = make_pass();
    pass.SetScissors(Scissors { .left = 2, .top = 0, .right = 4, .bottom = 4 });
    EXPECT_THROW(pass.SetViewport(ViewPort {
                   .top_left_x = 0.0F,
                   .top_left_y = 0.0F,
                   .width = 2.0F,
                   .height = 4.0F,
                   .min_depth = 0.0F,
                   .max_depth = 1.0F,
                 }),
      std::invalid_argument);
  }
}

NOLINT_TEST_F(DepthPrePassConfigTest, ViewportScissorsIntersectionIsEffective)
{
  auto depth_texture = CreateDepthTexture(4U, 4U, "config.intersection");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "config.intersection",
    }));
  pass.SetViewport(ViewPort {
    .top_left_x = 1.0F,
    .top_left_y = 0.0F,
    .width = 3.0F,
    .height = 4.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  });
  pass.SetScissors(Scissors { .left = 0, .top = 1, .right = 4, .bottom = 3 });

  const auto output = pass.GetOutput();
  EXPECT_EQ(output.valid_rect.left, 1);
  EXPECT_EQ(output.valid_rect.top, 1);
  EXPECT_EQ(output.valid_rect.right, 4);
  EXPECT_EQ(output.valid_rect.bottom, 3);
}

NOLINT_TEST_F(DepthPrePassConfigTest, IsNotCompleteBeforeExecution)
{
  auto depth_texture = CreateDepthTexture(4U, 4U, "config.not-complete");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "config.not-complete",
    }));

  const auto output = pass.GetOutput();
  EXPECT_TRUE(output.has_depth_texture);
  EXPECT_FALSE(output.has_canonical_srv);
  EXPECT_FALSE(output.is_complete);
}

} // namespace
