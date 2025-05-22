//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>

#include <MainModule.h>

using oxygen::examples::MainModule;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;
using oxygen::graphics::Scissors;
using oxygen::graphics::ShaderType;
using oxygen::graphics::ViewPort;

namespace {
struct Vertex {
    float position[3];
    float color[3];
};

constexpr float kTriangleHeight = 0.5f;

// Helper to compute a simple upright triangle in NDC, centered at (cx, cy), with given height (NDC units, 1.0 = full framebuffer height) and rotation
inline void ComputeEquilateralTriangle(Vertex (&out_vertices)[3], float cx, float cy, float height, float angle_rad)
{
    // Simple upright triangle: top at (cx, cy + height/2), base at (cx - base/2, cy - height/2) and (cx + base/2, cy - height/2)
    float half_height = height / 2.0f;
    float base = height; // For a simple triangle, base = height (not equilateral, but fills NDC)
    glm::vec2 verts[3] = {
        { cx, cy + half_height }, // top
        { cx - base / 2.0f, cy - half_height }, // bottom left
        { cx + base / 2.0f, cy - half_height } // bottom right
    };
    float cos_a = std::cos(angle_rad);
    float sin_a = std::sin(angle_rad);
    for (int i = 0; i < 3; ++i) {
        float x = verts[i].x - cx;
        float y = verts[i].y - cy;
        float xr = x * cos_a - y * sin_a;
        float yr = x * sin_a + y * cos_a;
        out_vertices[i].position[0] = cx + xr;
        out_vertices[i].position[1] = cy + yr;
        out_vertices[i].position[2] = 0.0f;
    }
    // Assign colors (RGB)
    out_vertices[0].color[0] = 1.0f;
    out_vertices[0].color[1] = 0.0f;
    out_vertices[0].color[2] = 0.0f;
    out_vertices[1].color[0] = 0.0f;
    out_vertices[1].color[1] = 1.0f;
    out_vertices[1].color[2] = 0.0f;
    out_vertices[2].color[0] = 0.0f;
    out_vertices[2].color[1] = 0.0f;
    out_vertices[2].color[2] = 1.0f;
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

    // Compute initial triangle data (unrotated)
    ComputeEquilateralTriangle(triangle_vertices, 0.0f, 0.0f, kTriangleHeight, 0.0f);

    graphics::BufferDesc vb_desc;
    vb_desc.size_bytes = sizeof(triangle_vertices);
    vb_desc.usage = graphics::BufferUsage::kVertex;
    vb_desc.memory = graphics::BufferMemory::kUpload;
    vb_desc.debug_name = "Triangle Vertex Buffer";
    vertex_buffer_ = renderer_->CreateBuffer(vb_desc);
    vertex_buffer_->SetName("Triangle Vertex Buffer");
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
    if (vertex_shader_ && pixel_shader_)
        return;

    CHECK_F(!gfx_weak_.expired());
    auto gfx = gfx_weak_.lock();

    // Load the engine shaders using the Graphics backend
    vertex_shader_ = gfx->GetShader(graphics::MakeShaderIdentifier(
        graphics::ShaderType::kVertex,
        "FullScreenTriangle.hlsl"));

    pixel_shader_ = gfx->GetShader(graphics::MakeShaderIdentifier(
        graphics::ShaderType::kPixel,
        "FullScreenTriangle.hlsl"));

    CHECK_NOTNULL_F(vertex_shader_, "Failed to load FullScreenTriangle vertex shader");
    CHECK_NOTNULL_F(pixel_shader_, "Failed to load FullScreenTriangle pixel shader");

    LOG_F(INFO, "Engine shaders loaded successfully");
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
    auto gfx = gfx_weak_.lock();
    auto recorder = renderer_->AcquireCommandRecorder(
        graphics::SingleQueueStrategy().GraphicsQueueName(),
        "Main Window Command List");

    auto fb = framebuffers_[renderer_->CurrentFrameIndex()];

    fb->PrepareForRender(*recorder);
    recorder->BindFrameBuffer(*fb);

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

    recorder->ClearFramebuffer(
        *fb,
        std::vector<std::optional<graphics::Color>> { graphics::Color { 0.09f, 0.48f, 0.38f, 1.0f } },
        std::nullopt,
        std::nullopt);

    CreateTriangleVertexBuffer();
    UpdateRotatingTriangle();

    // Update the vertex buffer with the new vertex positions
    if (vertex_buffer_) {
        void* mapped_data = vertex_buffer_->Map(0, sizeof(triangle_vertices));
        if (mapped_data) {
            std::memcpy(mapped_data, triangle_vertices, sizeof(triangle_vertices));
            vertex_buffer_->UnMap();
        } else {
            LOG_F(ERROR, "Failed to map vertex buffer for updating!");
        }
    }

    // Set the pipeline state with our shaders. Should be called after
    // framebuffer, viewport, and scissors are set, and before resource binding
    // and draw calls.
    recorder->SetPipelineState(vertex_shader_, pixel_shader_);

    // Bind the vertex buffer
    graphics::BufferPtr vertex_buffers[] = { vertex_buffer_ };
    uint32_t strides[] = { sizeof(Vertex) };
    uint32_t offsets[] = { 0 };
    recorder->SetVertexBuffers(1, vertex_buffers, strides, offsets);

    // Draw the triangle
    recorder->Draw(static_cast<uint32_t>(std::size(triangle_vertices)), 1, 0, 0);

    co_return;
}

void MainModule::UpdateRotatingTriangle()
{
    // Increment rotation angle (in radians)
    rotation_angle_ += 0.03f;
    if (rotation_angle_ > glm::two_pi<float>()) {
        rotation_angle_ -= glm::two_pi<float>();
    }
    // Compute rotated equilateral triangle (updates triangle_vertices)
    ComputeEquilateralTriangle(
        triangle_vertices, 0.0f, 0.0f,
        kTriangleHeight, rotation_angle_);
}
