//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Graphics/Direct3d12/Renderer.h"

#include <cstdint>
#include <cstring>
#include <d3d12.h>
#include <dxgiformat.h>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include <wrl/client.h>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/ResourceTable.h"
#include "Oxygen/Core/Types.h"
#include "Oxygen/Graphics/Common/ObjectRelease.h"
#include "Oxygen/Graphics/Common/RenderTarget.h"
#include "Oxygen/Graphics/Common/Renderer.h"
#include "Oxygen/Graphics/Common/ShaderByteCode.h"
#include "Oxygen/Graphics/Common/ShaderCompiler.h"
#include "Oxygen/Graphics/Common/ShaderManager.h"
#include "Oxygen/Graphics/Common/Shaders.h"
#include "Oxygen/Graphics/Common/Types.h"
#include "Oxygen/Graphics/Direct3D12/Buffer.h"
#include "Oxygen/Graphics/Direct3D12/D3D12MemAlloc.h"
#include "Oxygen/Graphics/Direct3d12/CommandQueue.h"
#include "Oxygen/Graphics/Direct3d12/CommandRecorder.h"
#include "Oxygen/Graphics/Direct3d12/DebugLayer.h"
#include "Oxygen/Graphics/Direct3d12/Detail/DescriptorHeap.h"
#include "Oxygen/Graphics/Direct3d12/Detail/WindowSurfaceImpl.h"
#include "Oxygen/Graphics/Direct3d12/ImGui/ImGuiModule.h"
#include "Oxygen/Graphics/Direct3d12/ShaderCompiler.h"
#include "Oxygen/Graphics/Direct3d12/Types.h"
#include "Oxygen/Graphics/Direct3d12/WindowSurface.h"
#include "Oxygen/ImGui/ImGuiPlatformBackend.h" // needed
#include "Oxygen/ImGui/ImguiModule.h"
#include "Oxygen/Platform/Common/Types.h"
#include <Oxygen/Base/Windows/ComError.h> // needed

using Microsoft::WRL::ComPtr;
using oxygen::graphics::ShaderType;
using oxygen::graphics::d3d12::DeviceType;
using oxygen::graphics::d3d12::FactoryType;
using oxygen::graphics::d3d12::detail::GetMainDevice;
using oxygen::windows::ThrowOnFailed;

namespace {
using oxygen::graphics::resources::kSurface;
oxygen::ResourceTable<oxygen::graphics::d3d12::detail::WindowSurfaceImpl> surfaces(kSurface, 256);
} // namespace

namespace {
// Specification of engine shaders. Each entry is a ShaderProfile
// corresponding to one of the shaders we want to automatically compile,
// package and load.
const oxygen::graphics::ShaderProfile kEngineShaders[] = {
  { .type = ShaderType::kPixel, .path = "FullScreenTriangle.hlsl", .entry_point = "PS" },
  { .type = ShaderType::kVertex, .path = "FullScreenTriangle.hlsl", .entry_point = "VS" },
};
} // namespace

// Implementation details of the Renderer class
namespace oxygen::graphics::d3d12::detail {
// Anonymous namespace for command frame management
namespace {

  struct CommandFrame {
    uint64_t fence_value { 0 };
  };

} // namespace

class RendererImpl final
{
 public:
  RendererImpl() = default;
  ~RendererImpl() = default;

  OXYGEN_MAKE_NON_COPYABLE(RendererImpl);
  OXYGEN_MAKE_NON_MOVEABLE(RendererImpl);

  auto CurrentFrameIndex() const -> size_t { return current_frame_index_; }

  void Init(const RendererProperties& props);
  void ShutdownRenderer();

  auto BeginFrame(const resources::SurfaceId& surface_id) const
    -> const graphics::RenderTarget&;
  void EndFrame(CommandLists& command_lists, const resources::SurfaceId& surface_id) const;
  auto CreateWindowSurfaceImpl(platform::WindowPtr window) const
    -> std::pair<resources::SurfaceId, std::shared_ptr<WindowSurfaceImpl>>;

  [[nodiscard]] auto RtvHeap() const -> DescriptorHeap& { return rtv_heap_; }
  [[nodiscard]] auto DsvHeap() const -> DescriptorHeap& { return dsv_heap_; }
  [[nodiscard]] auto SrvHeap() const -> DescriptorHeap& { return srv_heap_; }
  [[nodiscard]] auto UavHeap() const -> DescriptorHeap& { return uav_heap_; }

  CommandRecorderPtr GetCommandRecorder() { return command_recorder_; }
  ShaderCompilerPtr GetShaderCompiler() const { return std::dynamic_pointer_cast<graphics::ShaderCompiler>(shader_compiler_); }
  std::shared_ptr<IShaderByteCode> GetEngineShader(std::string_view unique_id);

 private:
  std::shared_ptr<ShaderCompiler> shader_compiler_ {};
  std::unique_ptr<ShaderManager> engine_shaders_ {};

  std::unique_ptr<CommandQueue> command_queue_ {};
  std::shared_ptr<CommandRecorder> command_recorder_ {};
  mutable size_t current_frame_index_ { 0 };
  mutable CommandFrame frames_[kFrameBufferCount] {};

  // DeferredReleaseControllerPtr GetWeakPtr() { return shared_from_this(); }

  mutable DescriptorHeap rtv_heap_ { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, "RTV Descriptor Heap" };
  mutable DescriptorHeap dsv_heap_ { D3D12_DESCRIPTOR_HEAP_TYPE_DSV, "DSV Descriptor Heap" };
  mutable DescriptorHeap srv_heap_ { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, "SRV Descriptor Heap" };
  mutable DescriptorHeap uav_heap_ { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, "UAV Descriptor Heap" };

#if defined(_DEBUG)
  DebugLayer debug_layer_ {};
#endif
};

void RendererImpl::Init(const RendererProperties& props)
{
  // Initialize the command recorder
  command_queue_.reset(new CommandQueue(CommandListType::kGraphics));
  command_queue_->Initialize();
  command_recorder_.reset(new CommandRecorder(CommandListType::kGraphics));
  command_recorder_->Initialize();

  // Initialize heaps
  rtv_heap_.Initialize(512, false, GetMainDevice());
  dsv_heap_.Initialize(512, false, GetMainDevice());
  srv_heap_.Initialize(4096, true, GetMainDevice());
  uav_heap_.Initialize(512, false, GetMainDevice());

  // Load engine shaders
  shader_compiler_ = std::make_shared<ShaderCompiler>(ShaderCompilerConfig {});
  shader_compiler_->Initialize();
  // TODO: Make this better by not hard-coding the path
  ShaderManagerConfig shader_manager_config {
    .renderer_name = "D3D12 Renderer",
    .archive_dir = R"(F:\projects\DroidNet\projects\Oxygen.Engine\bin\Oxygen)",
    .source_dir = R"(F:\projects\DroidNet\projects\Oxygen.Engine\Oxygen\Graphics\Direct3D12\Shaders)",
    .shaders = std::span(kEngineShaders, std::size(kEngineShaders)),
    .compiler = shader_compiler_,
  };
  engine_shaders_ = std::make_unique<ShaderManager>(std::move(shader_manager_config));
  engine_shaders_->Initialize();
}

void RendererImpl::ShutdownRenderer()
{
  LOG_SCOPE_FUNCTION(INFO);

  // Cleanup engine shaders
  shader_compiler_->IsInitialized(false);
  ObjectRelease(shader_compiler_);
  engine_shaders_->Shutdown();
  engine_shaders_.reset();

  // Flush any pending commands and release any deferred resources for all
  // our frame indices
  command_queue_->Flush();

  srv_heap_.Release();
  uav_heap_.Release();
  dsv_heap_.Release();
  rtv_heap_.Release();

  command_queue_->Release();
  command_queue_.reset();
  command_recorder_->Release();
  command_recorder_.reset();

  // TODO: SafeRelease for objects that need to be released after a full flush
  // otherwise, we should use ObjectRelease() or DeferredObjectRelease()
  LOG_F(INFO, "D3D12MA Memory Allocator released");
}

auto RendererImpl::BeginFrame(const resources::SurfaceId& surface_id) const
  -> const graphics::RenderTarget&
{
  DCHECK_NOTNULL_F(command_recorder_);

  // Wait for the GPU to finish executing the previous frame, reset the
  // allocator once the GPU is done with it to free the memory we allocated to
  // store the commands.
  const auto& fence_value = frames_[CurrentFrameIndex()].fence_value;
  command_queue_->Wait(fence_value);

  DCHECK_F(surface_id.IsValid());

  auto& surface = surfaces.ItemAt(surface_id);
  if (surface.ShouldResize()) {
    command_queue_->Flush();
    surface.Resize();
  }
  return surface;
}

void RendererImpl::EndFrame(CommandLists& command_lists, const resources::SurfaceId& surface_id) const
{
  try {
    const auto& surface = surfaces.ItemAt(surface_id);

    command_queue_->Submit(command_lists);
    for (auto& command_list : command_lists) {
      command_list->Release();
      command_list.reset();
    }
    command_lists.clear();

    // Presenting
    surface.Present();
  } catch (const std::exception& e) {
    LOG_F(WARNING, "No surface for id=`{}`; frame discarded: {}", surface_id.ToString(), e.what());
  }

  // Signal and increment the fence value for the next frame.
  frames_[CurrentFrameIndex()].fence_value = command_queue_->Signal();
  current_frame_index_ = (current_frame_index_ + 1) % kFrameBufferCount;
}

auto RendererImpl::CreateWindowSurfaceImpl(platform::WindowPtr window) const -> std::pair<resources::SurfaceId, std::shared_ptr<WindowSurfaceImpl>>
{
  DCHECK_NOTNULL_F(window.lock());
  DCHECK_F(window.lock()->IsValid());

  const auto surface_id = surfaces.Emplace(std::move(window), command_queue_->GetCommandQueue());
  if (!surface_id.IsValid()) {
    return {};
  }
  LOG_F(INFO, "Window Surface created: {}", surface_id.ToString());

  // Use a custom deleter to call Erase when the shared_ptr<WindowSurfaceImpl> is destroyed
  auto deleter = [surface_id](WindowSurfaceImpl* ptr) {
    surfaces.Erase(surface_id);
  };
  auto& surface_impl = surfaces.ItemAt(surface_id);
  return { surface_id, { &surface_impl, deleter } };
}

std::shared_ptr<IShaderByteCode> RendererImpl::GetEngineShader(std::string_view unique_id)
{
  return engine_shaders_->GetShaderBytecode(unique_id);
}

} // namespace oxygen::graphics::d3d12::detail

using oxygen::graphics::d3d12::Renderer;

Renderer::Renderer()
  : graphics::Renderer("D3D12 Renderer")
{
}

auto Renderer::CreateVertexBuffer(const void* data, size_t size, uint32_t stride) const -> BufferPtr
{
  DCHECK_NOTNULL_F(data);
  DCHECK_GT_F(size, 0u);
  DCHECK_GT_F(stride, 0u);

  // Create the vertex buffer resource
  D3D12_RESOURCE_DESC resourceDesc = {};
  resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  resourceDesc.Alignment = 0;
  resourceDesc.Width = size;
  resourceDesc.Height = 1;
  resourceDesc.DepthOrArraySize = 1;
  resourceDesc.MipLevels = 1;
  resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
  resourceDesc.SampleDesc.Count = 1;
  resourceDesc.SampleDesc.Quality = 0;
  resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  D3D12MA::ALLOCATION_DESC allocDesc = {};
  allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

  BufferInitInfo initInfo = {
    .alloc_desc = allocDesc,
    .resource_desc = resourceDesc,
    .initial_state = D3D12_RESOURCE_STATE_GENERIC_READ,
    .size_in_bytes = size
  };

  auto buffer = std::make_shared<Buffer>();
  buffer->Initialize(initInfo);

  // Copy the vertex data to the buffer
  void* mappedData = buffer->Map();
  memcpy(mappedData, data, size);
  buffer->Unmap();

  return buffer;
}

void Renderer::OnInitialize(PlatformPtr platform, const RendererProperties& props)
{
  if (IsInitialized())
    OnShutdown();

  graphics::Renderer::OnInitialize(std::move(platform), props);
  pimpl_ = std::make_shared<detail::RendererImpl>();
  try {
    pimpl_->Init(GetInitProperties());
  } catch (const std::runtime_error&) {
    // Request a shutdown to cleanup resources
    IsInitialized(true);
    throw;
  }
}

void Renderer::OnShutdown()
{
  pimpl_->ShutdownRenderer();
  graphics::Renderer::OnShutdown();
}

auto Renderer::BeginFrame(const resources::SurfaceId& surface_id)
  -> const graphics::RenderTarget&
{
  current_render_target_ = &pimpl_->BeginFrame(surface_id);
  return *current_render_target_;
}

void Renderer::EndFrame(CommandLists& command_lists, const resources::SurfaceId& surface_id) const
{
  pimpl_->EndFrame(command_lists, surface_id);
}

auto Renderer::CreateImGuiModule(EngineWeakPtr engine, platform::WindowIdType window_id) const -> std::unique_ptr<imgui::ImguiModule>
{
  return std::make_unique<ImGuiModule>(std::move(engine), window_id);
}

auto Renderer::GetCommandRecorder() const -> CommandRecorderPtr
{
  return pimpl_->GetCommandRecorder();
}

auto Renderer::GetShaderCompiler() const -> ShaderCompilerPtr
{
  return pimpl_->GetShaderCompiler();
}

auto Renderer::GetEngineShader(std::string_view unique_id) const -> std::shared_ptr<IShaderByteCode>
{
  return pimpl_->GetEngineShader(unique_id);
}

auto Renderer::RtvHeap() const -> detail::DescriptorHeap&
{
  return pimpl_->RtvHeap();
}

auto Renderer::DsvHeap() const -> detail::DescriptorHeap&
{
  return pimpl_->DsvHeap();
}

auto Renderer::SrvHeap() const -> detail::DescriptorHeap&
{
  return pimpl_->SrvHeap();
}

auto Renderer::UavHeap() const -> detail::DescriptorHeap&
{
  return pimpl_->UavHeap();
}

auto Renderer::CreateWindowSurface(platform::WindowPtr window) const -> SurfacePtr
{
  DCHECK_NOTNULL_F(window.lock());
  DCHECK_F(window.lock()->IsValid());

  const auto [surface_id, surface_impl] { pimpl_->CreateWindowSurfaceImpl(window) };
  if (!surface_impl) {
    return {};
  }
  return SurfacePtr(new WindowSurface(surface_id, std::move(window), surface_impl));
}
