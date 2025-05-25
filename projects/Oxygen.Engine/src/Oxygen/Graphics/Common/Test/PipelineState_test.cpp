//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::graphics;

namespace {

//! Test GraphicsPipelineDesc builder pattern
NOLINT_TEST(GraphicsPipelineDescTest, BuilderBasicUsage)
{ // Create a basic graphics pipeline using the builder pattern
    const auto pipeline
        = GraphicsPipelineDesc::Builder()
              .SetVertexShader({ "test_vertex_shader" })
              .SetPixelShader({ "test_pixel_shader" })
              .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
              .Build(); // Verify the shaders were set
    EXPECT_TRUE(pipeline.VertexShader().has_value());
    EXPECT_EQ(pipeline.VertexShader()->shader, "test_vertex_shader");
    EXPECT_TRUE(pipeline.PixelShader().has_value());
    EXPECT_EQ(pipeline.PixelShader()->shader, "test_pixel_shader");

    // Verify optional shaders are not set
    EXPECT_FALSE(pipeline.GeometryShader().has_value());
    EXPECT_FALSE(pipeline.HullShader().has_value());
    EXPECT_FALSE(pipeline.DomainShader().has_value());
}

NOLINT_TEST(GraphicsPipelineDescTest, BuilderFullConfiguration)
{
    const auto pipeline
        = GraphicsPipelineDesc::Builder {}
              .SetVertexShader({ "test_vs", "VSMain" })
              .SetPixelShader({ "test_ps", "PSMain" })
              .SetGeometryShader({ "test_gs", "GSMain" })
              .SetPrimitiveTopology(PrimitiveType::kTriangleStrip)
              .SetRasterizerState(
                  { .fill_mode = FillMode::kWireFrame,
                      .cull_mode = CullMode::kNone,
                      .multisample_enable = true })
              .SetDepthStencilState(
                  { .depth_test_enable = true,
                      .depth_write_enable = true,
                      .depth_func = CompareOp::kLess })
              .AddBlendTarget(
                  { .blend_enable = true,
                      .src_blend = BlendFactor::kSrcAlpha,
                      .dest_blend = BlendFactor::kInvSrcAlpha,
                      .blend_op = BlendOp::kAdd })
              .SetFramebufferLayout(
                  { .color_target_formats = { Format::kRGBA8UNorm },
                      .depth_stencil_format = Format::kDepth32,
                      .sample_count = 1 })
              .Build(); // Verify full configuration
    EXPECT_TRUE(pipeline.VertexShader().has_value());
    EXPECT_EQ(pipeline.VertexShader()->shader, "test_vs");
    EXPECT_EQ(pipeline.VertexShader()->entry_point_name, "VSMain");

    EXPECT_TRUE(pipeline.PixelShader().has_value());
    EXPECT_EQ(pipeline.PixelShader()->shader, "test_ps");
    EXPECT_EQ(pipeline.PixelShader()->entry_point_name, "PSMain");

    EXPECT_TRUE(pipeline.GeometryShader().has_value());
    EXPECT_EQ(pipeline.GeometryShader()->shader, "test_gs");
    EXPECT_EQ(pipeline.GeometryShader()->entry_point_name, "GSMain");

    EXPECT_EQ(pipeline.PrimitiveTopology(), PrimitiveType::kTriangleStrip);

    const auto& raster = pipeline.RasterizerState();
    EXPECT_EQ(raster.fill_mode, FillMode::kWireFrame);
    EXPECT_EQ(raster.cull_mode, CullMode::kNone);
    EXPECT_TRUE(raster.multisample_enable);

    const auto& depth = pipeline.DepthStencilState();
    EXPECT_TRUE(depth.depth_test_enable);
    EXPECT_TRUE(depth.depth_write_enable);
    EXPECT_EQ(depth.depth_func, CompareOp::kLess);

    EXPECT_EQ(pipeline.BlendState().size(), 1);
    const auto& blend = pipeline.BlendState()[0];
    EXPECT_TRUE(blend.blend_enable);
    EXPECT_EQ(blend.src_blend, BlendFactor::kSrcAlpha);
    EXPECT_EQ(blend.dest_blend, BlendFactor::kInvSrcAlpha);
    EXPECT_EQ(blend.blend_op, BlendOp::kAdd);

    const auto& fb = pipeline.FramebufferLayout();
    EXPECT_EQ(fb.color_target_formats.size(), 1);
    EXPECT_EQ(fb.color_target_formats[0], Format::kRGBA8UNorm);
    EXPECT_EQ(fb.depth_stencil_format, Format::kDepth32);
    EXPECT_EQ(fb.sample_count, 1);
}

NOLINT_TEST(GraphicsPipelineDescTest, MultipleBlendTargets)
{
    const auto pipeline
        = GraphicsPipelineDesc::Builder {}
              .SetVertexShader({ "test_vs" })
              .SetPixelShader({ "test_ps" })
              .AddBlendTarget({ .blend_enable = true,
                  .src_blend = BlendFactor::kOne,
                  .dest_blend = BlendFactor::kOne,
                  .blend_op = BlendOp::kAdd })
              .AddBlendTarget({ .blend_enable = false,
                  .write_mask = ColorWriteMask::kR | ColorWriteMask::kG })
              .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm, Format::kRGBA8UNorm } })
              .Build();
    EXPECT_EQ(pipeline.BlendState().size(), 2);

    const auto& blend0 = pipeline.BlendState()[0];
    EXPECT_TRUE(blend0.blend_enable);
    EXPECT_EQ(blend0.src_blend, BlendFactor::kOne);
    EXPECT_EQ(blend0.dest_blend, BlendFactor::kOne);
    EXPECT_EQ(blend0.blend_op, BlendOp::kAdd);

    const auto& blend1 = pipeline.BlendState()[1];
    EXPECT_FALSE(blend1.blend_enable);
    EXPECT_EQ(blend1.write_mask, ColorWriteMask::kR | ColorWriteMask::kG);
}

NOLINT_TEST(ComputePipelineDescTest, BuilderBasicUsage)
{
    const auto pipeline
        = ComputePipelineDesc::Builder()
              .SetComputeShader({ "test_compute", "CSMain" })
              .Build();
    EXPECT_EQ(pipeline.ComputeShader().shader, "test_compute");
    EXPECT_EQ(pipeline.ComputeShader().entry_point_name, "CSMain");
}

NOLINT_TEST(ComputePipelineDescTest, MissingShaderThrows)
{
    NOLINT_EXPECT_THROW([[maybe_unused]] auto pipeline
        = ComputePipelineDesc::Builder {}.Build(),
        std::runtime_error);
}

NOLINT_TEST(GraphicsPipelineDescTest, MissingVertexShaderThrows)
{
    NOLINT_EXPECT_THROW(
        [[maybe_unused]] auto pipeline = GraphicsPipelineDesc::Builder {}
            .SetPixelShader({ "test_ps" })
            .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
            .Build(),
        std::runtime_error);
}

NOLINT_TEST(GraphicsPipelineDescTest, MissingPixelShaderThrows)
{
    NOLINT_EXPECT_THROW(
        [[maybe_unused]] auto pipeline = GraphicsPipelineDesc::Builder {}
            .SetVertexShader({ "test_vs" })
            .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
            .Build(),
        std::runtime_error);
}

NOLINT_TEST(GraphicsPipelineDescTest, EmptyFramebufferLayoutThrows)
{
    NOLINT_EXPECT_THROW(
        [[maybe_unused]] auto pipeline = GraphicsPipelineDesc::Builder {}
            .SetVertexShader({ "test_vs" })
            .SetPixelShader({ "test_ps" })
            .SetFramebufferLayout({}) // Empty framebuffer layout
            .Build(),
        std::runtime_error);
}

NOLINT_TEST(GraphicsPipelineDescTest, ValidMinimalConfiguration)
{
    // This should not throw - minimal valid configuration with color target
    const auto pipeline
        = GraphicsPipelineDesc::Builder {}
              .SetVertexShader({ "test_vs" })
              .SetPixelShader({ "test_ps" })
              .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
              .Build();

    EXPECT_TRUE(pipeline.VertexShader().has_value());
    EXPECT_TRUE(pipeline.PixelShader().has_value());
    EXPECT_EQ(pipeline.FramebufferLayout().color_target_formats.size(), 1);
}

NOLINT_TEST(GraphicsPipelineDescTest, ValidDepthOnlyConfiguration)
{
    // This should not throw - depth-only configuration is valid
    const auto pipeline
        = GraphicsPipelineDesc::Builder {}
              .SetVertexShader({ "test_vs" })
              .SetPixelShader({ "test_ps" })
              .SetFramebufferLayout({ .depth_stencil_format = Format::kDepth32 })
              .Build();

    EXPECT_TRUE(pipeline.VertexShader().has_value());
    EXPECT_TRUE(pipeline.PixelShader().has_value());
    EXPECT_TRUE(pipeline.FramebufferLayout().depth_stencil_format.has_value());
    EXPECT_EQ(*pipeline.FramebufferLayout().depth_stencil_format, Format::kDepth32);
    EXPECT_TRUE(pipeline.FramebufferLayout().color_target_formats.empty());
}

} // namespace
