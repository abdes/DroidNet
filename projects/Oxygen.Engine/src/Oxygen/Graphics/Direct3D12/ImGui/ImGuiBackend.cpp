//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/ImGui/ImGuiBackend.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/ImGui/imgui_impl_dx12.h>

#include <imgui.h>

#include <stdexcept>

using oxygen::graphics::d3d12::D3D12ImGuiGraphicsBackend;

/*!
 Initialize the ImGui D3D12 backend with dedicated descriptor heap.

 Creates a dedicated CBV_SRV_UAV descriptor heap for ImGui textures and
 initializes the official imgui_impl_dx12 backend using callback-based
 descriptor allocation.

 @param gfx_weak Graphics system instance (must be D3D12Graphics)

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: Allocates dedicated descriptor heap (64 descriptors)
 - Optimization: Single allocation for all ImGui descriptors

 @throw std::runtime_error if initialization fails
 @see ImGui_ImplDX12_Init
*/
auto D3D12ImGuiGraphicsBackend::Init(std::weak_ptr<oxygen::Graphics> gfx_weak)
  -> void
{
  DCHECK_F(!gfx_weak.expired());

  // Cast to D3D12 graphics to access D3D12-specific functionality
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  const auto gfx = gfx_weak.lock();
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  auto* d3d_gfx = static_cast<Graphics*>(gfx.get());
  auto* device = d3d_gfx->GetCurrentDevice();

  // Get graphics queue for ImGui initialization
  const auto graphics_queue = d3d_gfx->GetCommandQueue(QueueRole::kGraphics);
  if (!graphics_queue) {
    throw std::runtime_error("Failed to get graphics command queue");
  }

  // Get the underlying D3D12 command queue
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  auto* d3d_queue = static_cast<const CommandQueue*>(graphics_queue.get());
  auto* command_queue = d3d_queue->GetCommandQueue();

  // Cache a pointer to the per-frame DeferredReclaimer so we can use it to
  // schedule deferred cleanup (GPU-safe) when shutting down. We query the
  // Graphics instance for the reclaimer component and retain a non-owning
  // observer_ptr here. Only cache if the Graphics instance owns a valid
  // DeferredReclaimer.
  try {
    reclaimer_
      = observer_ptr(&const_cast<Graphics*>(d3d_gfx)->GetDeferredReclaimer());
  } catch (...) {
    // If for some reason the call fails (shouldn't happen) leave reclaimer_ as
    // nullptr and let Shutdown() fall back to immediate release.
    reclaimer_ = nullptr;
  }

  // Create dedicated descriptor heap for ImGui (CBV_SRV_UAV:imgui from config)
  constexpr UINT kImGuiDescriptorCount = 64;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc {};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = kImGuiDescriptorCount;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

  if (FAILED(device->CreateDescriptorHeap(
        &heap_desc, IID_PPV_ARGS(imgui_srv_heap_.ReleaseAndGetAddressOf())))) {
    throw std::runtime_error("Failed to create ImGui descriptor heap");
  }

  imgui_descriptor_increment_ = device->GetDescriptorHandleIncrementSize(
    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  next_descriptor_index_ = 0;

  // Setup ImGui initialization info with callback-based descriptor allocation
  init_info_ = std::make_unique<ImGui_ImplDX12_InitInfo>();
  init_info_->Device = device;
  init_info_->CommandQueue = command_queue;
  init_info_->NumFramesInFlight = 3; // Match engine frame count
  init_info_->RTVFormat
    = DXGI_FORMAT_R8G8B8A8_UNORM; // Default, may be overridden
  init_info_->DSVFormat = DXGI_FORMAT_D32_FLOAT; // Default, may be overridden
  init_info_->SrvDescriptorHeap = imgui_srv_heap_.Get();
  init_info_->SrvDescriptorAllocFn = &SrvDescriptorAllocCallback;
  init_info_->SrvDescriptorFreeFn = &SrvDescriptorFreeCallback;
  init_info_->UserData = this; // Pass this instance for callbacks

  // Create and configure ImGui context
  IMGUI_CHECKVERSION();
  imgui_context_ = ImGui::CreateContext();
  ImGui::SetCurrentContext(imgui_context_);
  ImGui::StyleColorsDark();

  if (!ImGui_ImplDX12_Init(init_info_.get())) {
    throw std::runtime_error("Failed to initialize ImGui D3D12 backend");
  }

  initialized_ = true;
}

/*!
 Shutdown the ImGui D3D12 backend and release resources.

 Calls the official imgui_impl_dx12 shutdown and releases the dedicated
 descriptor heap and ImGui context.
*/
auto D3D12ImGuiGraphicsBackend::Shutdown() -> void
{
  if (initialized_) {
    // If a deferred reclaimer is available, register a deferred action that
    // performs the full ImGui backend shutdown on the renderer thread when
    // the frame bucket cycles. This avoids final-releasing pipeline/root
    // signature objects while the GPU might still reference them.
    if (reclaimer_) {
      LOG_F(1,
        "D3D12 ImGui backend deferring shutdown via DeferredReclaimer: "
        "context={} device={} srv_heap={}",
        static_cast<void*>(imgui_context_),
        static_cast<void*>(init_info_ ? init_info_->Device : nullptr),
        static_cast<void*>(imgui_srv_heap_.Get()));

      // Keep a reference to the descriptor heap alive for the deferred
      // action and capture the ImGui context pointer so the shutdown lambda
      // can access and destroy the ImGui backend data safely later.
      auto heap_keepalive = imgui_srv_heap_;
      ImGuiContext* context_keepalive = imgui_context_;

      // Register an action that runs on the renderer thread when the
      // DeferredReclaimer processes the frame bucket ensuring the GPU is no
      // longer using the resources.
      reclaimer_->RegisterDeferredAction(
        [heap_keepalive = std::move(heap_keepalive),
          context_keepalive]() mutable {
          try {
            if (context_keepalive) {
              ImGui::SetCurrentContext(context_keepalive);
            }
            LOG_F(1,
              "D3D12 ImGui backend: deferred ImGui_ImplDX12_Shutdown()"
              " running");
            ImGui_ImplDX12_Shutdown();
            LOG_F(1,
              "D3D12 ImGui backend: deferred ImGui_ImplDX12_Shutdown()"
              " completed");
            if (context_keepalive) {
              ImGui::DestroyContext(context_keepalive);
            }
          } catch (const std::exception& e) {
            LOG_F(ERROR, "Exception in deferred ImGui shutdown: {}", e.what());
          } catch (...) {
            LOG_F(ERROR, "Unknown exception in deferred ImGui shutdown");
          }
        });

      // Mark as no longer initialized and clear pointers on the backend
      // side right away. The deferred action owns the heap and context
      // until it runs.
      initialized_ = false;
      imgui_context_ = nullptr;
      init_info_.reset();
      imgui_srv_heap_.Reset();
      next_descriptor_index_ = 0;
    } else {
      // No reclaimer available — fall back to immediate shutdown to avoid
      // leaking resources.
      ImGui::SetCurrentContext(imgui_context_);
      LOG_F(1,
        "D3D12 ImGui backend shutting down: context={} device={} "
        "pPipelineState={} srv_heap={}",
        static_cast<void*>(imgui_context_),
        static_cast<void*>(init_info_->Device), static_cast<void*>(nullptr),
        static_cast<void*>(init_info_->SrvDescriptorHeap));
      ImGui_ImplDX12_Shutdown();
      LOG_F(1, "D3D12 ImGui backend: ImGui_ImplDX12_Shutdown returned");
      initialized_ = false;
    }
  }

  if (imgui_context_) {
    ImGui::DestroyContext(imgui_context_);
    imgui_context_ = nullptr;
  }

  imgui_srv_heap_.Reset();
  init_info_.reset();
  next_descriptor_index_ = 0;
}

/*!
 Begin a new ImGui frame.

 Sets the current ImGui context and calls the official imgui_impl_dx12 NewFrame
 function, followed by ImGui::NewFrame().
*/
auto D3D12ImGuiGraphicsBackend::NewFrame() -> void
{
  if (!imgui_context_ || !initialized_) {
    return;
  }

  ImGui::SetCurrentContext(imgui_context_);
  ImGui_ImplDX12_NewFrame();
  ImGui::NewFrame();
}

/*!
 Record ImGui draw commands to the command recorder.

 Gets the current ImGui draw data and delegates to the official imgui_impl_dx12
 backend to render it.

 @param recorder Command recorder to record draw commands to

 ### Performance Characteristics

 - Time Complexity: O(n) where n is number of draw commands
 - Memory: No additional allocations
 - Optimization: Direct delegation to optimized imgui_impl_dx12

 @note This function expects the command recorder to have an active D3D12
       command list that can be cast to ID3D12GraphicsCommandList*
*/
auto D3D12ImGuiGraphicsBackend::Render(graphics::CommandRecorder& recorder)
  -> void
{
  if (!imgui_context_ || !initialized_) {
    return;
  }

  // Set the context and get current draw data
  ImGui::SetCurrentContext(imgui_context_);
  ImGui::Render();
  ImDrawData* current_draw_data = ImGui::GetDrawData();

  if (!current_draw_data) {
    return;
  }

  // Cast the command recorder to D3D12 to get the underlying command list
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  const auto* d3d_recorder = static_cast<CommandRecorder*>(&recorder);
  auto* command_list = d3d_recorder->GetD3D12CommandList();

  // Set the ImGui descriptor heap on the command list
  ID3D12DescriptorHeap* heaps[] = { imgui_srv_heap_.Get() };
  command_list->SetDescriptorHeaps(1, heaps);

  // Delegate to the official ImGui D3D12 backend
  ImGui_ImplDX12_RenderDrawData(current_draw_data, command_list);
}

auto D3D12ImGuiGraphicsBackend::RecreateDeviceObjects() -> void
{
  DLOG_F(1,
    "D3D12ImGuiGraphicsBackend::RecreateDeviceObjects called: initialized={} "
    "next_descriptor_index={} srv_heap={}",
    static_cast<int>(initialized_), next_descriptor_index_,
    static_cast<void*>(imgui_srv_heap_.Get()));
  if (!initialized_) {
    return;
  }

  // Make sure we operate on the correct ImGui context
  if (imgui_context_) {
    ImGui::SetCurrentContext(imgui_context_);
  }

  // Invalidate device objects in ImGui D3D12 backend and try to recreate.
  ImGui_ImplDX12_InvalidateDeviceObjects();
  DLOG_F(1,
    "D3D12ImGuiGraphicsBackend::RecreateDeviceObjects - invalidated device "
    "objects; now recreating");
  // Reset the linear descriptor allocator used by the imgui_impl_dx12
  // callback allocator. We must reset this on recreate so repeated
  // RecreateDeviceObjects calls (e.g. during refactoring or duplicate
  // notifications) don't monotonically consume the finite heap and
  // eventually exhaust it.
  next_descriptor_index_ = 0;
  if (!ImGui_ImplDX12_CreateDeviceObjects()) {
    LOG_F(WARNING, "ImGui D3D12 backend failed to recreate device objects");
  }
}

// --- Descriptor allocation callbacks ---

/*!
 Allocate a descriptor from the ImGui-dedicated heap.

 This callback is called by imgui_impl_dx12 when it needs to allocate
 descriptors for textures (primarily the font atlas).

 @param info ImGui initialization info containing UserData with backend instance
 @param out_cpu_handle Output CPU descriptor handle
 @param out_gpu_handle Output GPU descriptor handle
*/
auto D3D12ImGuiGraphicsBackend::SrvDescriptorAllocCallback(
  // ReSharper disable once CppParameterMayBeConstPtrOrRef
  ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
  D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) -> void
{
  auto* backend = static_cast<D3D12ImGuiGraphicsBackend*>(info->UserData);
  DCHECK_NOTNULL_F(backend);

  if (backend->next_descriptor_index_ >= 64) {
    LOG_F(ERROR, "ImGui descriptor heap exhausted");
    return;
  }

  const UINT index = backend->next_descriptor_index_++;
  const UINT offset = index * backend->imgui_descriptor_increment_;

  *out_cpu_handle
    = backend->imgui_srv_heap_->GetCPUDescriptorHandleForHeapStart();
  out_cpu_handle->ptr += offset;

  *out_gpu_handle
    = backend->imgui_srv_heap_->GetGPUDescriptorHandleForHeapStart();
  out_gpu_handle->ptr += offset;
}

/*!
 Free a descriptor from the ImGui-dedicated heap.

 This callback is called by imgui_impl_dx12 when it no longer needs a
 descriptor. Currently, this is a no-op since we use a simple linear allocator
 for the ImGui heap.

 @param info ImGui initialization info (unused)
 @param cpu_handle CPU descriptor handle to free (unused)
 @param gpu_handle GPU descriptor handle to free (unused)

 @note In a production implementation, this could implement a more sophisticated
       allocation strategy with reuse.
*/
auto D3D12ImGuiGraphicsBackend::SrvDescriptorFreeCallback(
  ImGui_ImplDX12_InitInfo* /*info*/, D3D12_CPU_DESCRIPTOR_HANDLE /*cpu_handle*/,
  D3D12_GPU_DESCRIPTOR_HANDLE /*gpu_handle*/) -> void
{
  // No-op: simple linear allocator, no reuse currently
}
