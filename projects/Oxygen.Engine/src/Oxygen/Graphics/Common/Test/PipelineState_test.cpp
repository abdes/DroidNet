//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Testing/GTest.h>
#include <gmock/gmock.h>
#include <unordered_map>
#include <unordered_set>

using namespace oxygen::graphics;

namespace {

// Custom matcher for RootBindingItem that ignores root_parameter_index_
// NOLINTNEXTLINE(*)
MATCHER_P(HasSameDescription, expected, "RootBindingItem has same description fields")
{
    return arg.binding_slot_desc == expected.binding_slot_desc && arg.visibility == expected.visibility && arg.data == expected.data;
}

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

NOLINT_TEST(GraphicsPipelineDescTest, AddRootBinding)
{
    // Test push constants binding
    RootBindingItem push({ .binding_slot_desc = { .register_index = 0, .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = PushConstantsBinding { .size = 16 } });
    // Test direct buffer binding
    RootBindingItem buffer({ .binding_slot_desc = { .register_index = 1, .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = DirectBufferBinding {} });
    // Test direct texture binding
    RootBindingItem texture({ .binding_slot_desc = { .register_index = 2, .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = DirectTextureBinding {} });
    // Test descriptor table binding with explicit range
    RootBindingItem table({ .binding_slot_desc = { .register_index = 3, .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = DescriptorTableBinding { .view_type = ResourceViewType::kTexture_SRV, .base_index = 5, .count = 8 } });

    // Add each binding individually
    auto pipeline = GraphicsPipelineDesc::Builder {}
                        .SetVertexShader({ "vs" })
                        .SetPixelShader({ "ps" })
                        .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
                        .AddRootBinding(push)
                        .AddRootBinding(buffer)
                        .AddRootBinding(texture)
                        .AddRootBinding(table)
                        .Build();
    auto span = pipeline.RootBindings();
    EXPECT_EQ(span.size(), 4);
    EXPECT_THAT(span[0], HasSameDescription(push));
    EXPECT_THAT(span[1], HasSameDescription(buffer));
    EXPECT_THAT(span[2], HasSameDescription(texture));
    EXPECT_THAT(span[3], HasSameDescription(table));
    // Check descriptor table binding range
    const auto& table_binding = std::get<DescriptorTableBinding>(span[3].data);
    EXPECT_EQ(table_binding.base_index, 5u);
    EXPECT_EQ(table_binding.count, 8u);
    // Check root parameter indices
    for (size_t i = 0; i < span.size(); ++i) {
        EXPECT_EQ(span[i].GetRootParameterIndex(), i);
    }

    // Add all at once using SetRootBindings
    std::vector<RootBindingItem> all = { push, buffer, texture, table };
    auto pipeline2 = GraphicsPipelineDesc::Builder {}
                         .SetVertexShader({ "vs" })
                         .SetPixelShader({ "ps" })
                         .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
                         .SetRootBindings(all)
                         .Build();
    auto span2 = pipeline2.RootBindings();
    EXPECT_EQ(span2.size(), 4);
    EXPECT_THAT(span2[0], HasSameDescription(push));
    EXPECT_THAT(span2[1], HasSameDescription(buffer));
    EXPECT_THAT(span2[2], HasSameDescription(texture));
    EXPECT_THAT(span2[3], HasSameDescription(table));
    // Check root parameter indices
    for (size_t i = 0; i < span2.size(); ++i) {
        EXPECT_EQ(span2[i].GetRootParameterIndex(), i);
    }

    // Mutual exclusion: AddRootBinding after SetRootBindings should throw
    std::vector<RootBindingItem> bindings = { push };
    EXPECT_THROW(
        (void)GraphicsPipelineDesc::Builder {}
            .SetVertexShader({ "vs" })
            .SetPixelShader({ "ps" })
            .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
            .SetRootBindings(bindings)
            .AddRootBinding(buffer),
        std::logic_error);
    // Mutual exclusion: SetRootBindings after AddRootBinding should throw
    EXPECT_THROW(
        (void)GraphicsPipelineDesc::Builder {}
            .SetVertexShader({ "vs" })
            .SetPixelShader({ "ps" })
            .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
            .AddRootBinding(push)
            .SetRootBindings(bindings),
        std::logic_error);
}

NOLINT_TEST(ComputePipelineDescTest, AddRootBinding)
{
    // Test push constants binding
    RootBindingItem push({ .binding_slot_desc = { .register_index = 0, .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = PushConstantsBinding { .size = 8 } });
    // Test direct buffer binding
    RootBindingItem buffer({ .binding_slot_desc = { .register_index = 1, .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = DirectBufferBinding {} });
    // Test direct texture binding
    RootBindingItem texture({ .binding_slot_desc = { .register_index = 2, .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = DirectTextureBinding {} });
    // Test descriptor table binding
    RootBindingItem table({ .binding_slot_desc = { .register_index = 3, .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = DescriptorTableBinding { .view_type = ResourceViewType::kTexture_SRV, .base_index = 7, .count = 32 } });

    // Add each binding individually
    auto pipeline = ComputePipelineDesc::Builder {}
                        .SetComputeShader({ "cs" })
                        .AddRootBinding(push)
                        .AddRootBinding(buffer)
                        .AddRootBinding(texture)
                        .AddRootBinding(table)
                        .Build();
    auto span = pipeline.RootBindings();
    EXPECT_EQ(span.size(), 4);
    EXPECT_THAT(span[0], HasSameDescription(push));
    EXPECT_THAT(span[1], HasSameDescription(buffer));
    EXPECT_THAT(span[2], HasSameDescription(texture));
    EXPECT_THAT(span[3], HasSameDescription(table));
    // Check root parameter indices
    for (size_t i = 0; i < span.size(); ++i) {
        EXPECT_EQ(span[i].GetRootParameterIndex(), i);
    }

    // Add all at once using SetRootBindings
    std::vector<RootBindingItem> all = { push, buffer, texture, table };
    auto pipeline2 = ComputePipelineDesc::Builder {}
                         .SetComputeShader({ "cs" })
                         .SetRootBindings(all)
                         .Build();
    auto span2 = pipeline2.RootBindings();
    EXPECT_EQ(span2.size(), 4);
    EXPECT_THAT(span2[0], HasSameDescription(push));
    EXPECT_THAT(span2[1], HasSameDescription(buffer));
    EXPECT_THAT(span2[2], HasSameDescription(texture));
    EXPECT_THAT(span2[3], HasSameDescription(table));
    // Check root parameter indices
    for (size_t i = 0; i < span2.size(); ++i) {
        EXPECT_EQ(span2[i].GetRootParameterIndex(), i);
    }

    // Mutual exclusion: AddRootBinding after SetRootBindings should throw
    std::vector<RootBindingItem> bindings = { push };
    EXPECT_THROW(
        (void)ComputePipelineDesc::Builder {}
            .SetComputeShader({ "cs" })
            .SetRootBindings(bindings)
            .AddRootBinding(buffer),
        std::logic_error);
    // Mutual exclusion: SetRootBindings after AddRootBinding should throw
    EXPECT_THROW(
        (void)ComputePipelineDesc::Builder {}
            .SetComputeShader({ "cs" })
            .AddRootBinding(push)
            .SetRootBindings(bindings),
        std::logic_error);
}

NOLINT_TEST(RootBindingItemTest, RootParameterIndexAssignment)
{
    RootBindingItem item({ .binding_slot_desc = { .register_index = 0, .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = PushConstantsBinding { .size = 4 } });
    EXPECT_EQ(item.GetRootParameterIndex(), ~0u);
    item.SetRootParameterIndex(5);
    EXPECT_EQ(item.GetRootParameterIndex(), 5u);
    // Setting again should throw
    EXPECT_THROW(item.SetRootParameterIndex(6), std::logic_error);
}

NOLINT_TEST(RootBindingItemTest, Equality)
{
    using oxygen::graphics::BindingSlotDesc;
    using oxygen::graphics::DescriptorTableBinding;
    using oxygen::graphics::ResourceViewType;
    using oxygen::graphics::RootBindingItem;
    using oxygen::graphics::ShaderStageFlags;

    RootBindingItem a({ .binding_slot_desc = { .register_index = 1, .register_space = 2 },
        .visibility = ShaderStageFlags::kAll,
        .data = DescriptorTableBinding { .view_type = ResourceViewType::kTexture_SRV, .base_index = 0, .count = 16 } });
    RootBindingItem b = a;
    RootBindingItem c = a;
    c.binding_slot_desc = { .register_index = 2, .register_space = 2 };

    // Test equality
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

NOLINT_TEST(GraphicsPipelineDescTest, Hashing)
{
    // Minimal valid pipeline
    auto pipeline1 = GraphicsPipelineDesc::Builder {}
                         .SetVertexShader({ "vs" })
                         .SetPixelShader({ "ps" })
                         .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
                         .Build();
    auto pipeline2 = GraphicsPipelineDesc::Builder {}
                         .SetVertexShader({ "vs" })
                         .SetPixelShader({ "ps" })
                         .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
                         .Build();
    auto pipeline3 = GraphicsPipelineDesc::Builder {}
                         .SetVertexShader({ "vs2" })
                         .SetPixelShader({ "ps" })
                         .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
                         .Build();

    std::hash<GraphicsPipelineDesc> hasher;
    EXPECT_EQ(hasher(pipeline1), hasher(pipeline2));
    EXPECT_NE(hasher(pipeline1), hasher(pipeline3));

    // Test unordered_set
    std::unordered_set<GraphicsPipelineDesc, std::hash<GraphicsPipelineDesc>> set;
    set.insert(pipeline1);
    EXPECT_EQ(set.count(pipeline2), 1);
    EXPECT_EQ(set.count(pipeline3), 0);

    // Test unordered_map
    std::unordered_map<GraphicsPipelineDesc, int, std::hash<GraphicsPipelineDesc>> map;
    map[pipeline1] = 42;
    EXPECT_EQ(map[pipeline2], 42);
}

NOLINT_TEST(ComputePipelineDescTest, Hashing)
{
    auto pipeline1 = ComputePipelineDesc::Builder {}
                         .SetComputeShader({ "cs" })
                         .Build();
    auto pipeline2 = ComputePipelineDesc::Builder {}
                         .SetComputeShader({ "cs" })
                         .Build();
    auto pipeline3 = ComputePipelineDesc::Builder {}
                         .SetComputeShader({ "cs2" })
                         .Build();

    std::hash<ComputePipelineDesc> hasher;
    EXPECT_EQ(hasher(pipeline1), hasher(pipeline2));
    EXPECT_NE(hasher(pipeline1), hasher(pipeline3));

    // Test unordered_set
    std::unordered_set<ComputePipelineDesc, std::hash<ComputePipelineDesc>> set;
    set.insert(pipeline1);
    EXPECT_EQ(set.count(pipeline2), 1);
    EXPECT_EQ(set.count(pipeline3), 0);

    // Test unordered_map
    std::unordered_map<ComputePipelineDesc, int, std::hash<ComputePipelineDesc>> map;
    map[pipeline1] = 99;
    EXPECT_EQ(map[pipeline2], 99);
}

// Data-oriented hash sensitivity test for GraphicsPipelineDesc
NOLINT_TEST(GraphicsPipelineDescTest, Hashing_AllFieldsAffectHash)
{
    struct Baseline {
        ShaderStageDesc vs { "vs", "VSMain" };
        ShaderStageDesc ps { "ps", "PSMain" };
        ShaderStageDesc gs { "gs", "GSMain" };
        ShaderStageDesc hs { "hs", "HSMain" };
        ShaderStageDesc ds { "ds", "DSMain" };
        PrimitiveType primitive_topology = PrimitiveType::kTriangleStrip;
        RasterizerStateDesc rasterizer { .fill_mode = FillMode::kWireFrame, .cull_mode = CullMode::kNone, .multisample_enable = true };
        DepthStencilStateDesc depth_stencil { .depth_test_enable = true, .depth_write_enable = true, .depth_func = CompareOp::kLess };
        BlendTargetDesc blend_target { .blend_enable = true, .src_blend = BlendFactor::kSrcAlpha, .dest_blend = BlendFactor::kInvSrcAlpha, .blend_op = BlendOp::kAdd };
        FramebufferLayoutDesc framebuffer { .color_target_formats = { Format::kRGBA8UNorm }, .depth_stencil_format = Format::kDepth32, .sample_count = 4 };
        std::vector<RootBindingItem> root_bindings { RootBindingItem({ .binding_slot_desc = { 0, 0 }, .visibility = ShaderStageFlags::kAll, .data = PushConstantsBinding { 16 } }) };
    };

    auto build = [](const Baseline& b) {
        auto builder = GraphicsPipelineDesc::Builder {};
        builder = std::move(builder).SetVertexShader(b.vs);
        builder = std::move(builder).SetPixelShader(b.ps);
        builder = std::move(builder).SetGeometryShader(b.gs);
        builder = std::move(builder).SetHullShader(b.hs);
        builder = std::move(builder).SetDomainShader(b.ds);
        builder = std::move(builder).SetPrimitiveTopology(b.primitive_topology);
        builder = std::move(builder).SetRasterizerState(b.rasterizer);
        builder = std::move(builder).SetDepthStencilState(b.depth_stencil);
        builder = std::move(builder).AddBlendTarget(b.blend_target);
        builder = std::move(builder).SetFramebufferLayout(b.framebuffer);
        for (const auto& rb : b.root_bindings) {
            builder = std::move(builder).AddRootBinding(rb);
        }
        return std::move(builder).Build();
    };

    Baseline base;
    std::hash<GraphicsPipelineDesc> hasher;
    auto base_hash = hasher(build(base));

    struct Delta {
        const char* msg;
        std::function<void(Baseline&)> apply;
    };
    std::vector<Delta> deltas = {
        { "VertexShader.shader not included in hash", [](Baseline& b) { b.vs.shader = "vs2"; } },
        { "VertexShader.entry_point_name not included in hash", [](Baseline& b) { b.vs.entry_point_name = "VSMain2"; } },
        { "PixelShader.shader not included in hash", [](Baseline& b) { b.ps.shader = "ps2"; } },
        { "PixelShader.entry_point_name not included in hash", [](Baseline& b) { b.ps.entry_point_name = "PSMain2"; } },
        { "GeometryShader.shader not included in hash", [](Baseline& b) { b.gs.shader = "gs2"; } },
        { "GeometryShader.entry_point_name not included in hash", [](Baseline& b) { b.gs.entry_point_name = "GSMain2"; } },
        { "HullShader.shader not included in hash", [](Baseline& b) { b.hs.shader = "hs2"; } },
        { "HullShader.entry_point_name not included in hash", [](Baseline& b) { b.hs.entry_point_name = "HSMain2"; } },
        { "DomainShader.shader not included in hash", [](Baseline& b) { b.ds.shader = "ds2"; } },
        { "DomainShader.entry_point_name not included in hash", [](Baseline& b) { b.ds.entry_point_name = "DSMain2"; } },
        { "PrimitiveTopology not included in hash", [](Baseline& b) { b.primitive_topology = PrimitiveType::kLineList; } },
        { "RasterizerState.fill_mode not included in hash", [](Baseline& b) { b.rasterizer.fill_mode = FillMode::kSolid; } },
        { "RasterizerState.cull_mode not included in hash", [](Baseline& b) { b.rasterizer.cull_mode = CullMode::kFront; } },
        { "RasterizerState.multisample_enable not included in hash", [](Baseline& b) { b.rasterizer.multisample_enable = false; } },
        { "DepthStencilState.depth_test_enable not included in hash", [](Baseline& b) { b.depth_stencil.depth_test_enable = false; } },
        { "DepthStencilState.depth_write_enable not included in hash", [](Baseline& b) { b.depth_stencil.depth_write_enable = false; } },
        { "DepthStencilState.depth_func not included in hash", [](Baseline& b) { b.depth_stencil.depth_func = CompareOp::kGreater; } },
        { "BlendState.blend_enable not included in hash", [](Baseline& b) { b.blend_target.blend_enable = false; } },
        { "BlendState.src_blend not included in hash", [](Baseline& b) { b.blend_target.src_blend = BlendFactor::kOne; } },
        { "BlendState.dest_blend not included in hash", [](Baseline& b) { b.blend_target.dest_blend = BlendFactor::kZero; } },
        { "BlendState.blend_op not included in hash", [](Baseline& b) { b.blend_target.blend_op = BlendOp::kSubtract; } },
        { "FramebufferLayout.color_target_formats not included in hash", [](Baseline& b) { b.framebuffer.color_target_formats = { Format::kBGRA8UNorm }; } },
        { "FramebufferLayout.depth_stencil_format not included in hash", [](Baseline& b) { b.framebuffer.depth_stencil_format = Format::kDepth16; } },
        { "FramebufferLayout.sample_count not included in hash", [](Baseline& b) { b.framebuffer.sample_count = 8; } },
        { "FramebufferLayout.sample_quality not included in hash", [](Baseline& b) { b.framebuffer.sample_quality = 8; } },
        { "RootBindings not included in hash", [](Baseline& b) { b.root_bindings = { RootBindingItem({ .binding_slot_desc = { 0, 0 }, .visibility = ShaderStageFlags::kAll, .data = PushConstantsBinding { 32 } }) }; } },
        { "RootBindings not included in hash", [](Baseline& b) { b.root_bindings = { RootBindingItem({ .binding_slot_desc = { 0, 0 }, .visibility = ShaderStageFlags::kAll, .data = DescriptorTableBinding { .view_type = ResourceViewType::kTexture_UAV, .base_index = 100, .count = 250 } }) }; } },
    };

    for (const auto& delta : deltas) {
        Baseline mod = base;
        delta.apply(mod);
        auto mod_hash = hasher(build(mod));
        EXPECT_NE(mod_hash, base_hash) << delta.msg;
    }
}

// Data-oriented hash sensitivity test for ComputePipelineDesc
NOLINT_TEST(ComputePipelineDescTest, Hashing_AllFieldsAffectHash)
{
    struct Baseline {
        ShaderStageDesc cs { "cs", "CSMain" };
        std::vector<RootBindingItem> root_bindings { RootBindingItem({ .binding_slot_desc = { 0, 0 }, .visibility = ShaderStageFlags::kAll, .data = PushConstantsBinding { 8 } }) };
    };

    auto build = [](const Baseline& b) {
        auto builder = ComputePipelineDesc::Builder {};
        builder = std::move(builder).SetComputeShader(b.cs);
        for (const auto& rb : b.root_bindings) {
            builder = std::move(builder).AddRootBinding(rb);
        }
        return std::move(builder).Build();
    };

    Baseline base;
    std::hash<ComputePipelineDesc> hasher;
    auto base_hash = hasher(build(base));

    struct Delta {
        const char* msg;
        std::function<void(Baseline&)> apply;
    };
    std::vector<Delta> deltas = {
        { "ComputeShader.shader not included in hash", [](Baseline& b) { b.cs.shader = "cs2"; } },
        { "ComputeShader.entry_point_name not included in hash", [](Baseline& b) { b.cs.entry_point_name = "CSMain2"; } },
        { "RootBindings not included in hash", [](Baseline& b) { b.root_bindings = { RootBindingItem({ .binding_slot_desc = { 0, 0 }, .visibility = ShaderStageFlags::kAll, .data = PushConstantsBinding { 16 } }) }; } },
        { "RootBindings not included in hash", [](Baseline& b) { b.root_bindings = { RootBindingItem({ .binding_slot_desc = { 0, 0 }, .visibility = ShaderStageFlags::kAll, .data = DescriptorTableBinding { .view_type = ResourceViewType::kTexture_UAV, .base_index = 100, .count = 250 } }) }; } },
    };

    for (const auto& delta : deltas) {
        Baseline mod = base;
        delta.apply(mod);
        auto mod_hash = hasher(build(mod));
        EXPECT_NE(mod_hash, base_hash) << delta.msg;
    }
}

} // namespace
