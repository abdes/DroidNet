//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>

#include <MainModule.h>

using oxygen::examples::MainModule;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::DeferredObjectRelease;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Scissors;
using oxygen::graphics::ShaderType;
using oxygen::graphics::ViewPort;

// ===================== DEBUGGING HISTORY & CONTRACTS =====================
//
// D3D12 Bindless Rendering Triangle: Lessons Learned (NEVER AGAIN!)
//
// 1. Culling & Winding Order:
//    - D3D12's default: counter-clockwise (CCW) triangles are front-facing.
//    - If your triangle is defined in clockwise (CW) order, it will be culled
//      (invisible) with default culling.
//    - Solution: Use CCW order for vertices, or set the rasterizer state to
//      match your winding.
//
// 2. Descriptor Table Offset vs. Heap Index:
//    - The SRV index written to the constant buffer (used by the shader) MUST
//      match the offset within the descriptor table bound for this draw, NOT
//      the global heap index. Index 0 is the first position in the bound table;
//      that is, the first descriptor after the CBV.
//    - If you use the global heap index, the shader will access the wrong
//      resource or nothing at all.
//    - Solution: Always write the offset within the currently bound descriptor
//      table to the constant buffer.
//
// CONTRACTS (DO NOT BREAK!):
// - Triangle vertices must be defined in CCW order for D3D12 default culling,
//   or the rasterizer state must be set to match.
// - The SRV index in the constant buffer must be the offset within the
//   descriptor table bound at draw time.
// - Do not confuse global heap indices with descriptor table offsets!
//
// If you see a blank screen or missing geometry, check these invariants first!
// ===========================================================================

namespace {
struct Vertex {
    float position[3];
    float color[3];
};

constexpr float kTriangleHeight = 0.8f;

// Store the initial triangle vertices (CCW order for D3D12 default culling)
Vertex initial_triangle_vertices[3] = {
    { { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, // Bottom left (green)
    { { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // Top (red)
    { { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } } // Bottom right (blue)
};

// Helper to rotate a triangle in NDC, centered at (cx, cy), with given height (NDC units, 1.0 = full framebuffer height) and rotation
inline void RotateTriangle(const Vertex (&in_vertices)[3], Vertex (&out_vertices)[3], float cx, float cy, float angle_rad)
{
    float cos_a = std::cos(angle_rad);
    float sin_a = std::sin(angle_rad);
    for (int i = 0; i < 3; ++i) {
        float x = in_vertices[i].position[0] - cx;
        float y = in_vertices[i].position[1] - cy;
        float xr = x * cos_a - y * sin_a;
        float yr = x * sin_a + y * cos_a;
        out_vertices[i].position[0] = cx + xr;
        out_vertices[i].position[1] = cy + yr;
        out_vertices[i].position[2] = in_vertices[i].position[2];
        out_vertices[i].color[0] = in_vertices[i].color[0];
        out_vertices[i].color[1] = in_vertices[i].color[1];
        out_vertices[i].color[2] = in_vertices[i].color[2];
    }
}

Vertex triangle_vertices[3];
}

MainModule::MainModule(
    std::shared_ptr<Platform> platform,
    std::weak_ptr<Graphics> gfx_weak)
    : platform_(std::move(platform))
    , gfx_weak_(std::move(gfx_weak))
{
    DCHECK_NOTNULL_F(platform_);
    DCHECK_F(!gfx_weak_.expired());
}

MainModule::~MainModule()
{
    LOG_SCOPE_F(INFO, "Destroying MainModule");

    // Flush command queues used for the surface
    if (!gfx_weak_.expired()) {
        auto gfx = gfx_weak_.lock();
        graphics::SingleQueueStrategy queues;
        gfx->GetCommandQueue(queues.GraphicsQueueName())->Flush();
    }

    // Unregister the vertex buffer view if it's valid
    // (No need to release descriptor handle, ResourceRegistry manages it)
    if (vertex_buffer_ && !gfx_weak_.expired() && renderer_) {
        try {
            auto& registry = renderer_->GetResourceRegistry();
            registry.UnRegisterViews(*vertex_buffer_);
        } catch (const std::exception& e) {
            LOG_F(ERROR, "Error while unregistering vertex buffer view: {}", e.what());
        }
    }

    vertex_buffer_.reset();
    framebuffers_.clear();
    surface_->DetachRenderer();
    renderer_.reset();
    surface_.reset();
    platform_.reset();
}

void MainModule::Run()
{
    DCHECK_NOTNULL_F(nursery_);
    SetupCommandQueues();
    SetupMainWindow();
    SetupSurface();
    SetupRenderer();
    SetupShaders();
    surface_->AttachRenderer(renderer_);

    nursery_->Start([this]() -> co::Co<> {
        while (!window_weak_.expired() && !gfx_weak_.expired()) {
            auto gfx = gfx_weak_.lock();
            co_await gfx->OnRenderStart();
            // Submit the render task to the renderer
            renderer_->Submit([this]() -> co::Co<> {
                co_await RenderScene();
            });
        }
    });
}

void MainModule::SetupCommandQueues() const
{
    CHECK_F(!gfx_weak_.expired());

    auto gfx = gfx_weak_.lock();
    gfx->CreateCommandQueues(graphics::SingleQueueStrategy());
}

void MainModule::SetupSurface()
{
    CHECK_F(!gfx_weak_.expired());
    CHECK_F(!window_weak_.expired());

    auto gfx = gfx_weak_.lock();

    graphics::SingleQueueStrategy queues;
    surface_ = gfx->CreateSurface(window_weak_, gfx->GetCommandQueue(queues.GraphicsQueueName()));
    surface_->SetName("Main Window Surface");
    LOG_F(INFO, "Surface ({}) created for main window ({})", surface_->GetName(), window_weak_.lock()->Id());
}

void MainModule::SetupMainWindow()
{
    // Set up the main window
    WindowProps props("Oxygen Graphics Example");
    props.extent = { .width = 800, .height = 800 };
    props.flags = {
        .hidden = false,
        .always_on_top = false,
        .full_screen = false,
        .maximized = false,
        .minimized = false,
        .resizable = true,
        .borderless = false
    };
    window_weak_ = std::move(platform_->Windows().MakeWindow(props));
    if (const auto window = window_weak_.lock()) {
        LOG_F(INFO, "Main window {} is created", window->Id());
    }

    // Immediately accept the close request for the main window
    nursery_->Start([this]() -> co::Co<> {
        while (!window_weak_.expired()) {
            auto window = window_weak_.lock();
            co_await window->CloseRequested();
            // Stop the nursery
            if (nursery_) {
                nursery_->Cancel();
            }
            window_weak_.lock()->VoteToClose();
        }
    });

    nursery_->Start([this]() -> co::Co<> {
        while (!window_weak_.expired()) {
            auto window = window_weak_.lock();
            if (const auto [from, to] = co_await window->Events().UntilChanged();
                to == WindowEvent::kResized) {
                LOG_F(INFO, "Main window was resized");
                surface_->ShouldResize(true);
                framebuffers_.clear();
            } else {
                if (to == WindowEvent::kExposed) {
                    LOG_F(INFO, "My window is exposed");
                }
            }
        }
    });

    // Add a termination signal handler
    nursery_->Start([this]() -> co::Co<> {
        co_await platform_->Async().OnTerminate();
        LOG_F(INFO, "terminating...");
        // Terminate the application by requesting the main window to close
        window_weak_.lock()->RequestClose();
    });
}

void MainModule::SetupRenderer()
{
    CHECK_F(!gfx_weak_.expired());

    auto gfx = gfx_weak_.lock();
    renderer_ = gfx->CreateRenderer("Main Window Renderer", surface_, kFrameBufferCount - 1);
    CHECK_NOTNULL_F(renderer_, "Failed to create renderer for main window");
}

void MainModule::CreateTriangleVertexBuffer()
{
    if (vertex_buffer_)
        return;

    graphics::BufferDesc vb_desc;
    vb_desc.size_bytes = sizeof(triangle_vertices);
    // For bindless rendering, use a structured buffer instead of vertex buffer
    vb_desc.usage = graphics::BufferUsage::kStorage;
    vb_desc.memory = graphics::BufferMemory::kDeviceLocal; // FIX: must be device local for UAV/storage
    vb_desc.debug_name = "Triangle Structured Vertex Buffer";
    vertex_buffer_ = renderer_->CreateBuffer(vb_desc);
    vertex_buffer_->SetName("Triangle Structured Vertex Buffer");

    auto& resource_registry = renderer_->GetResourceRegistry();
    resource_registry.Register(vertex_buffer_);

    recreate_cbv_ = true;
}

void MainModule::UploadTriangleVertexBuffer(graphics::CommandRecorder& recorder)
{
    if (!vertex_buffer_)
        return;

    // Create a temporary upload buffer
    graphics::BufferDesc upload_desc;
    upload_desc.size_bytes = sizeof(triangle_vertices);
    // The upload buffer must NOT have kStorage usage! Only use kNone or kVertex/kIndex if needed for upload.
    upload_desc.usage = graphics::BufferUsage::kNone;
    upload_desc.memory = graphics::BufferMemory::kUpload;
    upload_desc.debug_name = "Triangle Vertex Upload Buffer";
    auto upload_buffer = renderer_->CreateBuffer(upload_desc);

    // The initial state for vertex_buffer_ is COMMON (kCommon)
    recorder.BeginTrackingResourceState(*vertex_buffer_, ResourceStates::kCommon, true);
    recorder.RequireResourceState(*vertex_buffer_, ResourceStates::kCopyDest);

    recorder.FlushBarriers();

    // Map and copy data
    void* mapped = upload_buffer->Map();
    memcpy(mapped, triangle_vertices, sizeof(triangle_vertices));
    upload_buffer->UnMap();

    // Copy to GPU buffer (dst, dst_offset, src, src_offset, size)
    recorder.CopyBuffer(*vertex_buffer_, 0, *upload_buffer, 0, sizeof(triangle_vertices));

    // Transition the vertex buffer from CopyDest to ShaderResource state
    recorder.RequireResourceState(*vertex_buffer_, ResourceStates::kShaderResource);
    recorder.FlushBarriers();

    // Keep the upload buffer alive until the command list is executed
    DeferredObjectRelease(upload_buffer, renderer_->GetPerFrameResourceManager());
}

void MainModule::SetupFramebuffers()
{
    CHECK_F(!gfx_weak_.expired());

    auto gfx = gfx_weak_.lock();

    auto fb_desc = graphics::FramebufferDesc {}.AddColorAttachment(surface_->GetCurrentBackBuffer());

    for (auto i = 0U; i < kFrameBufferCount; ++i) {
        framebuffers_.push_back(renderer_->CreateFramebuffer(
            graphics::FramebufferDesc {}
                .AddColorAttachment(surface_->GetBackBuffer(i))));
        CHECK_NOTNULL_F(framebuffers_[i], "Failed to create framebuffer for main window");
    }
}

void MainModule::SetupShaders()
{
    CHECK_F(!gfx_weak_.expired());
    auto gfx = gfx_weak_.lock();

    // Verify that the shaders can be loaded by the Graphics backend
    auto vertex_shader = gfx->GetShader(graphics::MakeShaderIdentifier(
        graphics::ShaderType::kVertex,
        "FullScreenTriangle.hlsl"));

    auto pixel_shader = gfx->GetShader(graphics::MakeShaderIdentifier(
        graphics::ShaderType::kPixel,
        "FullScreenTriangle.hlsl"));

    CHECK_NOTNULL_F(vertex_shader, "Failed to load FullScreenTriangle vertex shader");
    CHECK_NOTNULL_F(pixel_shader, "Failed to load FullScreenTriangle pixel shader");

    LOG_F(INFO, "Engine shaders loaded successfully");
}

// === Bindless Rendering Invariants ===
// - The engine manages a single shader-visible CBV_SRV_UAV heap per D3D12 type.
// - The CBV for per-draw constants is always at heap index 0 (register b0).
// - All other resources (SRVs, UAVs) are placed in the heap starting at index 1.
// - For this example, the vertex buffer SRV is always at heap index 1 (register t0, space0).
// - The constant buffer (CBV) contains a uint specifying the index of the vertex buffer SRV in the heap (always 1 for this draw).
// - The root signature and shader are designed to match this layout. See PipelineStateCache.cpp and FullScreenTriangle.hlsl for details.

void MainModule::EnsureVertexBufferSRV()
{
    if (!recreate_cbv_) {
        return;
    }

    auto& resource_registry = renderer_->GetResourceRegistry();

    // The SRV for the vertex buffer is always allocated at heap index 1.
    // This index must match the value written to the CBV for the shader to access the correct buffer.
    graphics::BufferViewDescription srv_view_desc;
    srv_view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
    srv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    srv_view_desc.format = graphics::Format::kUnknown;
    srv_view_desc.stride = sizeof(Vertex);

    // This will create the SRV in the backend and return the handle
    // The correct way is to call GetNativeView with a DescriptorHandle and the view description.
    // However, to get a DescriptorHandle, you typically allocate it from the DescriptorAllocator.
    // Let's do this properly:
    auto& descriptor_allocator = renderer_->GetDescriptorAllocator();
    auto srv_handle = descriptor_allocator.Allocate(
        graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);

    if (!srv_handle.IsValid()) {
        LOG_F(ERROR, "Failed to allocate descriptor handle for vertex buffer SRV!");
        recreate_cbv_ = false;
        return;
    }

    // Actually create the native view (SRV) in the backend
    auto view = vertex_buffer_->GetNativeView(srv_handle, srv_view_desc);

    // -1 because the first index (0) is reserved for the constant buffer (b0)
    vertex_srv_shader_visible_index_ = descriptor_allocator.GetShaderVisibleIndex(srv_handle) - 1;

    // Register the view if not already registered
    resource_registry.RegisterView(*vertex_buffer_, std::move(view), std::move(srv_handle), srv_view_desc);

    LOG_F(INFO, "Vertex buffer SRV registered at index {}", vertex_srv_shader_visible_index_);
}

void MainModule::EnsureConstantBufferForVertexSRV()
{
    static graphics::DescriptorHandle cbv_handle_for_b0;

    if (!recreate_cbv_) {
        // No need to create the constant buffer if we don't have a renderer or
        // if we don't need to recreate it
        return;
    }

    auto& descriptor_allocator = renderer_->GetDescriptorAllocator();
    auto& resource_registry = renderer_->GetResourceRegistry();

    // Create the constant buffer if it doesn't exist
    bool created = false;
    if (!constant_buffer_) {
        DLOG_F(INFO, "Creating constant buffer for vertex buffer SRV index {}", vertex_srv_shader_visible_index_);
        graphics::BufferDesc cb_desc;
        cb_desc.size_bytes = 256; // D3D12 CBV alignment
        cb_desc.usage = graphics::BufferUsage::kConstant;
        cb_desc.memory = graphics::BufferMemory::kUpload;
        cb_desc.debug_name = "Vertex Buffer Index Constant Buffer";
        constant_buffer_ = renderer_->CreateBuffer(cb_desc);
        constant_buffer_->SetName("Vertex Buffer Index Constant Buffer");
        created = true;
    }

    // Always update the buffer contents (SRV index may change per frame)
    // Invariant: The value written here must match the SRV's offset within the descriptor table (not the global heap index).
    // For this example, the vertex buffer SRV is the first in the table, so the correct index is 0.
    void* mapped_data = constant_buffer_->Map();
    memcpy(mapped_data, &vertex_srv_shader_visible_index_, sizeof(vertex_srv_shader_visible_index_));
    constant_buffer_->UnMap();

    // Only unregister/register if the buffer was just created (not every frame)
    if (created) {
        resource_registry.UnRegisterResource(*constant_buffer_);
        resource_registry.Register(constant_buffer_);
        cbv_handle_for_b0 = {}; // force reallocation of CBV handle
    }

    // Register the CBV view only if not already registered
    if (!cbv_handle_for_b0.IsValid()) {
        // Do this before the re-allocation to recycle the index 0
        if (!created) {
            DLOG_F(INFO, "Unregister CBV for vertex buffer SRV index {} to recreate it", vertex_srv_shader_visible_index_);
            resource_registry.UnRegisterViews(*constant_buffer_);
        }
        DLOG_F(INFO, "Create CBV for vertex buffer SRV index {}", vertex_srv_shader_visible_index_);
        cbv_handle_for_b0 = descriptor_allocator.Allocate(
            graphics::ResourceViewType::kConstantBuffer,
            graphics::DescriptorVisibility::kShaderVisible);

        if (!cbv_handle_for_b0.IsValid()) {
            LOG_F(ERROR, "Failed to allocate descriptor handle for constant buffer view (b0)!");
            return;
        }

        graphics::BufferViewDescription cbv_view_desc;
        cbv_view_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
        cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
        cbv_view_desc.format = graphics::Format::kUnknown;
        cbv_view_desc.range.offset_bytes = 0;
        cbv_view_desc.range.size_bytes = constant_buffer_->GetSize();

        resource_registry.RegisterView(*constant_buffer_, std::move(cbv_handle_for_b0), cbv_view_desc);
    }

    recreate_cbv_ = false; // Reset the flag after creating the CBV
}

void MainModule::EnsureTriangleDrawResources()
{
    DCHECK_F(constant_buffer_ || recreate_cbv_, "Constant buffer must be created first");
    if (!constant_buffer_) {
        try {
            EnsureConstantBufferForVertexSRV();
            recreate_cbv_ = true; // Set the flag after creating the CBV for the first time
        } catch (const std::exception& e) {
            LOG_F(ERROR, "Error while ensuring CBV: {}", e.what());
            throw;
        }
    }

    DCHECK_F(constant_buffer_ != nullptr, "Constant buffer must be created first");
    try {
        // 1. Ensure the vertex buffer SRV is allocated and registered, get its index
        EnsureVertexBufferSRV();
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Error while ensuring vertex buffer SRV: {}", e.what());
        throw;
    }
    try {
        // 2. Ensure the constant buffer is created, registered, and updated with the SRV index
        EnsureConstantBufferForVertexSRV();
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Error while ensuring CBV: {}", e.what());
        throw;
    }
}

auto MainModule::RenderScene() -> co::Co<>
{
    if (gfx_weak_.expired()) {
        co_return;
    }

    if (framebuffers_.empty()) {
        SetupFramebuffers();
    }

    DLOG_F(1, "Rendering scene in frame index {}", renderer_->CurrentFrameIndex());

    // 1. Update triangle data (rotating every frame)
    rotation_angle_ += 0.01f;
    if (rotation_angle_ > glm::two_pi<float>())
        rotation_angle_ -= glm::two_pi<float>();
    RotateTriangle(initial_triangle_vertices, triangle_vertices, 0.0f, 0.0f, rotation_angle_);

    // 2. Create/ensure vertex buffer and descriptors
    try {
        // 3. Allocate and write descriptor handles
        CreateTriangleVertexBuffer();

    } catch (const std::exception& e) {
        LOG_F(ERROR, "Error while creating triangle vertex buffer: {}", e.what());
        co_return;
    }
    EnsureTriangleDrawResources();

    // 3. Reset/Begin the command list.
    auto gfx = gfx_weak_.lock();
    auto recorder = renderer_->AcquireCommandRecorder(
        graphics::SingleQueueStrategy().GraphicsQueueName(),
        "Main Window Command List");

    // 4. Upload vertex buffer data
    UploadTriangleVertexBuffer(*recorder);

    // 5. Prepare framebuffer, set viewport/scissors, pipeline, bindless, clear, draw
    auto fb = framebuffers_[renderer_->CurrentFrameIndex()];
    fb->PrepareForRender(*recorder);

    ViewPort viewport {
        .width = static_cast<float>(surface_->Width()),
        .height = static_cast<float>(surface_->Height()),
    };
    recorder->SetViewport(viewport);

    Scissors scissors {
        .right = static_cast<int32_t>(surface_->Width()),
        .bottom = static_cast<int32_t>(surface_->Height())
    };
    recorder->SetScissors(scissors);

    // 6. Set the root signature and pipeline state.

    // Create a framebuffer layout descriptor
    graphics::FramebufferLayoutDesc fbLayout;

    // Extract formats from the current framebuffer
    const auto& fbDesc = fb->GetDescriptor();
    for (const auto& colorAttachment : fbDesc.color_attachments) {
        if (colorAttachment.IsValid()) {
            fbLayout.color_target_formats.push_back(
                colorAttachment.format != graphics::Format::kUnknown
                    ? colorAttachment.format
                    : colorAttachment.texture->GetDescriptor().format);
        }
    }

    // Add depth format if present
    if (fbDesc.depth_attachment.IsValid()) {
        fbLayout.depth_stencil_format = fbDesc.depth_attachment.format != graphics::Format::kUnknown
            ? fbDesc.depth_attachment.format
            : fbDesc.depth_attachment.texture->GetDescriptor().format;
    } // Create a complete graphics pipeline state using the builder pattern
    // This is the new API that replaces the separate SetPipelineState(vertex_shader, pixel_shader)
    // Set rasterizer state: default is back-face culling, CW is front face
    graphics::RasterizerStateDesc rasterizerDesc = {};
    rasterizerDesc.cull_mode = graphics::CullMode::kBack; // Restore normal back-face culling
    rasterizerDesc.front_counter_clockwise = false; // CW is front face

    auto pipelineDesc
        = graphics::GraphicsPipelineDesc::Builder()
              .SetVertexShader(graphics::ShaderStageDesc {
                  graphics::MakeShaderIdentifier(graphics::ShaderType::kVertex, "FullScreenTriangle.hlsl") })
              .SetPixelShader(graphics::ShaderStageDesc {
                  graphics::MakeShaderIdentifier(graphics::ShaderType::kPixel, "FullScreenTriangle.hlsl") })
              .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
              .SetRasterizerState(rasterizerDesc)
              .SetFramebufferLayout(std::move(fbLayout))
              .Build();

    // Set the pipeline state. Should be called after framebuffer, viewport, and scissors
    // are set, and before resource binding and draw calls.
    recorder->SetPipelineState(pipelineDesc);

    // 7. Setup Bindless Rendering Tables
    recorder->SetupBindlessRendering();

    // 8. Draw the triangle
    recorder->ClearFramebuffer(
        *fb,
        std::vector<std::optional<graphics::Color>> { graphics::Color { 0.1f, 0.2f, 0.38f, 1.0f } },
        std::nullopt,
        std::nullopt);

    recorder->Draw(static_cast<uint32_t>(std::size(triangle_vertices)), 1, 0, 0);

    co_return;
}
