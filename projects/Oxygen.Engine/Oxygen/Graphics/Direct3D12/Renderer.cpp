//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Graphics/Direct3d12/Renderer.h"

#include <algorithm>
#include <functional>
#include <memory>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include "Oxygen/Base/ResourceTable.h"
#include "Oxygen/Base/Windows/ComError.h"
#include "Oxygen/Graphics/Common/CommandRecorder.h"
#include "Oxygen/Graphics/Common/ShaderManager.h"
#include "Oxygen/Graphics/Common/Types.h"
#include "Oxygen/Graphics/Direct3d12/CommandQueue.h"
#include "Oxygen/Graphics/Direct3d12/CommandRecorder.h"
#include "Oxygen/Graphics/Direct3d12/DebugLayer.h"
#include "Oxygen/Graphics/Direct3d12/Detail/DescriptorHeap.h"
#include "Oxygen/Graphics/Direct3d12/Detail/WindowSurfaceImpl.h"
#include "Oxygen/Graphics/Direct3d12/Graphics.h"
#include "Oxygen/Graphics/Direct3d12/ImGui/ImGuiModule.h"
#include "Oxygen/Graphics/Direct3d12/ShaderCompiler.h"
#include "Oxygen/Graphics/Direct3d12/Types.h"
#include "Oxygen/Graphics/Direct3d12/WindowSurface.h"
#include "Oxygen/ImGui/ImGuiPlatformBackend.h"

using Microsoft::WRL::ComPtr;
using oxygen::graphics::d3d12::detail::GetMainDevice;
using oxygen::renderer::d3d12::DeviceType;
using oxygen::renderer::d3d12::FactoryType;
using oxygen::windows::ThrowOnFailed;

namespace {
using oxygen::renderer::resources::kSurface;
oxygen::ResourceTable<oxygen::renderer::d3d12::detail::WindowSurfaceImpl> surfaces(kSurface, 256);
} // namespace

namespace {
// Specification of engine shaders. Each entry is a ShaderProfile
// corresponding to one of the shaders we want to automatically compile,
// package and load.
const oxygen::renderer::ShaderProfile kEngineShaders[] = {
  { .type = oxygen::ShaderType::kPixel, .path = "FullScreenTriangle.hlsl", .entry_point = "PS" },
  { .type = oxygen::ShaderType::kVertex, .path = "FullScreenTriangle.hlsl", .entry_point = "VS" },
};
} // namespace

// Implementation details of the Renderer class
namespace oxygen::renderer::d3d12::detail {
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
    -> const renderer::RenderTarget&;
  void EndFrame(CommandLists& command_lists, const resources::SurfaceId& surface_id) const;
  auto CreateWindowSurfaceImpl(platform::WindowPtr window) const
    -> std::pair<resources::SurfaceId, std::shared_ptr<WindowSurfaceImpl>>;

  [[nodiscard]] auto RtvHeap() const -> DescriptorHeap& { return rtv_heap_; }
  [[nodiscard]] auto DsvHeap() const -> DescriptorHeap& { return dsv_heap_; }
  [[nodiscard]] auto SrvHeap() const -> DescriptorHeap& { return srv_heap_; }
  [[nodiscard]] auto UavHeap() const -> DescriptorHeap& { return uav_heap_; }

  [[nodiscard]] D3D12MA::Allocator* GetAllocator() const { return allocator_; }
  CommandRecorderPtr GetCommandRecorder() { return command_recorder_; }
  ShaderCompilerPtr GetShaderCompiler() const { return std::dynamic_pointer_cast<renderer::ShaderCompiler>(shader_compiler_); }

 private:
  D3D12MA::Allocator* allocator_ { nullptr };
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

  ObjectRelease(allocator_);
  // TODO: SafeRelease for objects that need to be released after a full flush
  // otherwise, we should use ObjectRelease() or DeferredObjectRelease()
  LOG_F(INFO, "D3D12MA Memory Allocator released");
}

auto RendererImpl::BeginFrame(const resources::SurfaceId& surface_id) const
  -> const renderer::RenderTarget&
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

} // namespace oxygen::renderer::d3d12::detail

using oxygen::renderer::d3d12::Renderer;

Renderer::Renderer()
  : oxygen::Renderer("D3D12 Renderer")
{
}

void Renderer::OnInitialize(PlatformPtr platform, const RendererProperties& props)
{
  if (IsInitialized())
    OnShutdown();

  oxygen::Renderer::OnInitialize(std::move(platform), props);
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
  oxygen::Renderer::OnShutdown();
}

auto Renderer::BeginFrame(const resources::SurfaceId& surface_id)
  -> const renderer::RenderTarget&
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

auto Renderer::GetAllocator() const -> D3D12MA::Allocator*
{
  return pimpl_->GetAllocator();
}
