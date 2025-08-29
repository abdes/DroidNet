//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "GeometryRenderModule.h"

#include <chrono>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/ThreadPool.h>

#include "../Renderer/Graph/Resource.h"

namespace oxygen::engine::asyncsim {

GeometryRenderModule::GeometryRenderModule()
  : EngineModuleBase("GeometryRenderer",
      ModulePhases::ParallelWork | ModulePhases::FrameGraph,
      ModulePriorities::Normal)
{
}

auto GeometryRenderModule::Initialize(AsyncEngineSimulator& engine) -> co::Co<>
{
  // Store engine reference for later use
  engine_ = observer_ptr { &engine };

  LOG_F(INFO, "[GeometryRenderer] Initializing geometry rendering module");

  // Initialize example geometry data
  InitializeGeometryData();

  // Set default configuration
  config_ = GeometryConfig { .enable_depth_prepass = true,
    .enable_transparency = true,
    .enable_instancing = false,
    .max_instances = 1000 };

  is_initialized_ = true;

  LOG_F(INFO,
    "[GeometryRenderer] Geometry rendering module initialized with {} objects",
    geometry_objects_.size());
  co_return;
}

auto GeometryRenderModule::Shutdown() -> co::Co<>
{
  LOG_F(INFO, "[GeometryRenderer] Shutting down geometry rendering module");

  // Clear geometry data
  geometry_objects_.clear();
  is_initialized_ = false;

  LOG_F(INFO, "[GeometryRenderer] Geometry rendering module shutdown complete");
  co_return;
}

auto GeometryRenderModule::OnFrameGraph(FrameContext& context) -> co::Co<>
{
  LOG_F(2, "[GeometryRenderer] Contributing to render graph for frame {}",
    context.GetFrameIndex());

  auto render_graph_builder = context.GetRenderGraphBuilder();
  if (!render_graph_builder) {
    LOG_F(WARNING, "[GeometryRenderer] No render graph builder available");
    co_return;
  }

  // Create render resources
  CreateRenderResources(*render_graph_builder);

  // Add rendering passes based on configuration
  if (config_.enable_depth_prepass) {
    AddDepthPrepass(*render_graph_builder);
  }

  AddOpaquePass(*render_graph_builder);

  if (config_.enable_transparency) {
    AddTransparencyPass(*render_graph_builder);
  }

  // Add extra dummy passes to exercise scheduling & dependencies
  AddLightingPass(*render_graph_builder);
  AddPostProcessPass(*render_graph_builder);
  AddUIPass(*render_graph_builder);

  // Debug: log pass handles to verify they are registered before build
  LOG_F(2,
    "[GeometryRenderer] Pass handles: depth={} opaque={} transp={} light={} "
    "post={} ui={}",
    depth_prepass_.get(), opaque_pass_.get(), transparency_pass_.get(),
    lighting_pass_.get(), post_process_pass_.get(), ui_pass_.get());

  LOG_F(2,
    "[GeometryRenderer] Render graph contribution complete - "
    "DepthPrepass: {}, Transparency: {}",
    config_.enable_depth_prepass, config_.enable_transparency);

  co_return;
}

auto GeometryRenderModule::OnParallelWork(FrameContext& context) -> co::Co<>
{
  LOG_F(3, "[GeometryRenderer] Processing geometry in parallel for frame {}",
    context.GetFrameIndex());

  // Perform parallel geometry processing (culling, sorting, etc.)
  co_await context.GetThreadPool()->Run([this](auto) {
    std::this_thread::sleep_for(
      std::chrono::microseconds(150)); // Simulate processing

    // Update render statistics
    UpdateRenderStats();
  });

  LOG_F(3, "[GeometryRenderer] Parallel geometry processing complete");
  co_return;
}

auto GeometryRenderModule::CreateRenderResources(RenderGraphBuilder& builder)
  -> void
{
  LOG_F(3, "[GeometryRenderer] Creating render resources");

  // Create depth buffer (shared across views for depth prepass)
  TextureDesc depth_desc(1920, 1080, TextureDesc::Format::D32_Float,
    TextureDesc::Usage::DepthStencil);
  depth_buffer_
    = builder.CreateTexture("GeometryDepthBuffer", std::move(depth_desc),
      ResourceLifetime::FrameLocal, ResourceScope::PerView);

  // Create color buffer (per-view for final rendering)
  TextureDesc color_desc(1920, 1080, TextureDesc::Format::RGBA8_UNorm,
    TextureDesc::Usage::RenderTarget);
  color_buffer_
    = builder.CreateTexture("GeometryColorBuffer", std::move(color_desc),
      ResourceLifetime::FrameLocal, ResourceScope::PerView);

  // Create vertex buffer (shared across all views)
  BufferDesc vertex_desc(1024 * 1024, BufferDesc::Usage::VertexBuffer, 32);
  vertex_buffer_
    = builder.CreateBuffer("GeometryVertexBuffer", std::move(vertex_desc),
      ResourceLifetime::FrameLocal, ResourceScope::Shared);

  // Create index buffer (shared across all views)
  BufferDesc index_desc(256 * 1024, BufferDesc::Usage::IndexBuffer, 0);
  index_buffer_ = builder.CreateBuffer("GeometryIndexBuffer",
    std::move(index_desc), ResourceLifetime::FrameLocal, ResourceScope::Shared);

  LOG_F(3,
    "[GeometryRenderer] Render resources created - "
    "Depth: {}, Color: {}, Vertex: {}, Index: {}",
    depth_buffer_.get(), color_buffer_.get(), vertex_buffer_.get(),
    index_buffer_.get());

  // Create lighting and post-processing buffers if needed
  if (lighting_buffer_.get() == 0) {
    TextureDesc lighting_desc(1920, 1080, TextureDesc::Format::RGBA16_Float,
      TextureDesc::Usage::RenderTarget);
    lighting_buffer_
      = builder.CreateTexture("LightingBuffer", std::move(lighting_desc),
        ResourceLifetime::FrameLocal, ResourceScope::PerView);
    LOG_F(1, "[GeometryRenderer] Created lighting_buffer_ with handle {}",
      lighting_buffer_.get());
  }

  if (post_process_buffer_.get() == 0) {
    TextureDesc pp_desc(1920, 1080, TextureDesc::Format::RGBA8_UNorm,
      TextureDesc::Usage::RenderTarget);
    post_process_buffer_ = builder.CreateTexture("PostProcessBuffer",
      std::move(pp_desc), ResourceLifetime::FrameLocal, ResourceScope::PerView);
    LOG_F(1, "[GeometryRenderer] Created post_process_buffer_ with handle {}",
      post_process_buffer_.get());
  }
}

auto GeometryRenderModule::AddDepthPrepass(RenderGraphBuilder& builder) -> void
{
  LOG_F(3, "[GeometryRenderer] Adding depth prepass");

  depth_prepass_
    = builder.AddRasterPass("GeometryDepthPrepass", [this](PassBuilder& pass) {
        pass.SetScope(PassScope::PerView)
          .IterateAllViews()
          .SetExecutor(
            [this](TaskExecutionContext& ctx) { ExecuteDepthPrepass(ctx); })
          .Reads(vertex_buffer_)
          .Reads(index_buffer_)
          .Outputs(depth_buffer_);
      });
  LOG_F(3, "[GeometryRenderer] Depth prepass added with handle {}",
    depth_prepass_.get());
}

auto GeometryRenderModule::AddOpaquePass(RenderGraphBuilder& builder) -> void
{
  LOG_F(3, "[GeometryRenderer] Adding opaque geometry pass");

  opaque_pass_
    = builder.AddRasterPass("GeometryOpaquePass", [this](PassBuilder& pass) {
        pass.SetScope(PassScope::PerView)
          .IterateAllViews()
          .SetExecutor(
            [this](TaskExecutionContext& ctx) { ExecuteOpaqueGeometry(ctx); })
          .Reads(vertex_buffer_)
          .Reads(index_buffer_)
          .Outputs(color_buffer_);

        // Add depth prepass dependency if enabled
        if (config_.enable_depth_prepass) {
          pass.DependsOn(depth_prepass_).Reads(depth_buffer_);
        }
      });

  LOG_F(3, "[GeometryRenderer] Opaque pass added with handle {}",
    opaque_pass_.get());
}

auto GeometryRenderModule::AddTransparencyPass(RenderGraphBuilder& builder)
  -> void
{
  LOG_F(3, "[GeometryRenderer] Adding transparency pass");

  transparency_pass_ = builder.AddRasterPass(
    "GeometryTransparencyPass", [this](PassBuilder& pass) {
      pass.SetScope(PassScope::PerView)
        .IterateAllViews()
        .SetExecutor(
          [this](TaskExecutionContext& ctx) { ExecuteTransparency(ctx); })
        .DependsOn(opaque_pass_) // Render after opaque geometry
        .Reads(vertex_buffer_)
        .Reads(index_buffer_)
        .Reads(depth_buffer_) // Read depth for depth testing
        .Outputs(color_buffer_); // Blend with color buffer
    });

  LOG_F(3, "[GeometryRenderer] Transparency pass added with handle {}",
    transparency_pass_.get());
}

auto GeometryRenderModule::AddLightingPass(RenderGraphBuilder& builder) -> void
{
  LOG_F(3, "[GeometryRenderer] Adding lighting pass");
  lighting_pass_ = builder.AddRasterPass(
    "GeometryLightingPass", [this](PassBuilder& pass) {
      pass.SetScope(PassScope::PerView)
        .IterateAllViews()
        .SetExecutor(
          [this](TaskExecutionContext& ctx) { ExecuteLighting(ctx); })
        .Reads(color_buffer_) // GBuffer color
        .Reads(depth_buffer_) // Depth for lighting (e.g., reconstruct position)
        .Outputs(lighting_buffer_);
      // Depend on latest color-producing pass (transparency if enabled else
      // opaque)
      if (config_.enable_transparency) {
        pass.DependsOn(transparency_pass_);
      } else {
        pass.DependsOn(opaque_pass_);
      }
    });
  LOG_F(3, "[GeometryRenderer] Lighting pass added with handle {}",
    lighting_pass_.get());
}

auto GeometryRenderModule::AddPostProcessPass(RenderGraphBuilder& builder)
  -> void
{
  LOG_F(3, "[GeometryRenderer] Adding post-process pass");
  post_process_pass_ = builder.AddRasterPass(
    "GeometryPostProcessPass", [this](PassBuilder& pass) {
      pass.SetScope(PassScope::PerView)
        .IterateAllViews()
        .SetExecutor(
          [this](TaskExecutionContext& ctx) { ExecutePostProcess(ctx); })
        .Reads(lighting_buffer_)
        .Outputs(post_process_buffer_)
        .DependsOn(lighting_pass_);
    });
  LOG_F(3, "[GeometryRenderer] Post-process pass added with handle {}",
    post_process_pass_.get());
}

auto GeometryRenderModule::AddUIPass(RenderGraphBuilder& builder) -> void
{
  LOG_F(3, "[GeometryRenderer] Adding UI overlay pass");
  ui_pass_ = builder.AddRasterPass("GeometryUIPass", [this](PassBuilder& pass) {
    pass.SetScope(PassScope::PerView)
      .IterateAllViews()
      .SetExecutor([this](TaskExecutionContext& ctx) { ExecuteUI(ctx); })
      .Reads(post_process_buffer_)
      .Outputs(color_buffer_) // Composite back into main color buffer
      .DependsOn(post_process_pass_);
  });
  LOG_F(3, "[GeometryRenderer] UI pass added with handle {}", ui_pass_.get());
}

auto GeometryRenderModule::ExecuteDepthPrepass(TaskExecutionContext& ctx)
  -> void
{
  LOG_F(3, "[GeometryRenderer] Executing depth prepass for view '{}'",
    ctx.GetViewInfo().view_name);

  auto& cmd = ctx.GetCommandRecorder();

  // Clear depth buffer
  cmd.ClearDepthStencilView(depth_buffer_, 1.0f, 0);

  // Render geometry for depth only
  for (const auto& geometry : geometry_objects_) {
    if (geometry.vertex_count > 0) {
      cmd.DrawIndexedInstanced(geometry.index_count, geometry.instance_count);
    }
  }

  // Update statistics
  last_frame_stats_.depth_pass_draws
    = static_cast<uint32_t>(geometry_objects_.size());
}

auto GeometryRenderModule::ExecuteOpaqueGeometry(TaskExecutionContext& ctx)
  -> void
{
  LOG_F(3, "[GeometryRenderer] Executing opaque geometry for view '{}'",
    ctx.GetViewInfo().view_name);

  auto& cmd = ctx.GetCommandRecorder();

  // Clear color buffer if not using depth prepass
  if (!config_.enable_depth_prepass) {
    const std::vector<float> clear_color { 0.0f, 0.0f, 0.0f, 1.0f };
    cmd.ClearRenderTarget(color_buffer_, clear_color);
  }

  // Render opaque geometry
  uint32_t opaque_draws = 0;
  for (const auto& geometry : geometry_objects_) {
    if (geometry.vertex_count > 0) {
      cmd.DrawIndexedInstanced(geometry.index_count, geometry.instance_count);
      ++opaque_draws;
    }
  }

  // Update statistics
  last_frame_stats_.opaque_draws = opaque_draws;
}

auto GeometryRenderModule::ExecuteTransparency(TaskExecutionContext& ctx)
  -> void
{
  LOG_F(3, "[GeometryRenderer] Executing transparency for view '{}'",
    ctx.GetViewInfo().view_name);

  auto& cmd = ctx.GetCommandRecorder();

  // Render transparent geometry (back-to-front sorted)
  uint32_t transparent_draws = 0;
  for (const auto& geometry : geometry_objects_) {
    // In a real implementation, would check material transparency
    if (geometry.vertex_count > 0) {
      cmd.DrawIndexedInstanced(geometry.index_count, geometry.instance_count);
      ++transparent_draws;
    }
  }

  // Update statistics
  last_frame_stats_.transparent_draws = transparent_draws;
}

auto GeometryRenderModule::ExecuteLighting(TaskExecutionContext& ctx) -> void
{
  LOG_F(3, "[GeometryRenderer] Executing lighting for view '{}'",
    ctx.GetViewInfo().view_name);
  auto& cmd = ctx.GetCommandRecorder();
  // Simulate lighting work: sample GBuffer & depth
  cmd.ClearRenderTarget(lighting_buffer_, { 0.1f, 0.1f, 0.15f, 1.0f });
  last_frame_stats_.lighting_passes++;
}

auto GeometryRenderModule::ExecutePostProcess(TaskExecutionContext& ctx) -> void
{
  LOG_F(3, "[GeometryRenderer] Executing post-process for view '{}'",
    ctx.GetViewInfo().view_name);
  auto& cmd = ctx.GetCommandRecorder();
  cmd.ClearRenderTarget(post_process_buffer_, { 0.0f, 0.0f, 0.0f, 0.0f });
  last_frame_stats_.post_process_passes++;
}

auto GeometryRenderModule::ExecuteUI(TaskExecutionContext& ctx) -> void
{
  LOG_F(3, "[GeometryRenderer] Executing UI overlay for view '{}'",
    ctx.GetViewInfo().view_name);
  auto& cmd = ctx.GetCommandRecorder();
  // Composite UI: just a clear with alpha to simulate draw
  cmd.ClearRenderTarget(color_buffer_, { 0.05f, 0.05f, 0.05f, 0.0f });
  last_frame_stats_.ui_passes++;
}

auto GeometryRenderModule::UpdateRenderStats() -> void
{
  uint32_t total_vertices = 0;
  uint32_t total_instances = 0;

  for (const auto& geometry : geometry_objects_) {
    total_vertices += geometry.vertex_count;
    total_instances += geometry.instance_count;
  }

  last_frame_stats_.total_vertices = total_vertices;
  last_frame_stats_.total_instances = total_instances;
}

auto GeometryRenderModule::InitializeGeometryData() -> void
{
  LOG_F(2, "[GeometryRenderer] Initializing example geometry data");

  // Create some example geometry objects
  geometry_objects_ = {
    GeometryData {
      .vertex_count = 1024, .index_count = 1536, .instance_count = 1 }, // Cube
    GeometryData { .vertex_count = 2048,
      .index_count = 3072,
      .instance_count = 1 }, // Sphere
    GeometryData {
      .vertex_count = 512, .index_count = 768, .instance_count = 1 }, // Plane
    GeometryData {
      .vertex_count = 4096, .index_count = 6144, .instance_count = 1 }
    // Complex mesh
  };

  LOG_F(2, "[GeometryRenderer] Initialized {} geometry objects",
    geometry_objects_.size());
}

} // namespace oxygen::engine::asyncsim
