//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include "pch.h"

#include <EngineRunner.h>
#include <Utils/TokenHelpers.h>

// WinUI 3 ISwapChainPanelNative definition (desktop IID)
struct __declspec(uuid("63AAD0B8-7C24-40FF-85A8-640D944CC325"))
  ISwapChainPanelNative : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain* swapChain) = 0;
};

using namespace System;
using namespace System::Diagnostics;
using namespace System::Threading;
using namespace System::Threading::Tasks;
using namespace Microsoft::Extensions::Logging;
using namespace oxygen::interop::module;

using oxygen::engine::interop::LogInfoMessage;

namespace Oxygen::Interop {

  ref class SwapChainAttachState sealed {
  public:
    SwapChainAttachState(IntPtr panel, IntPtr swapChain, IntPtr surfaceHandle,
      float compositionScale)
      : panel_(panel), swap_chain_(swapChain), surface_handle_(surfaceHandle),
      composition_scale_(compositionScale) {
    }

    property IntPtr PanelPtr {
      IntPtr get() { return panel_; }
    }

    property IntPtr SwapChainPtr {
      IntPtr get() { return swap_chain_; }
    }

    property IntPtr SurfaceHandle {
      IntPtr get() { return surface_handle_; }
    }

    property float CompositionScale {
      float get() { return composition_scale_; }
    }

  private:
    IntPtr panel_;
    IntPtr swap_chain_;
    IntPtr surface_handle_;
    float composition_scale_;
  };

  auto EngineRunner::TryUnregisterSurfaceAsync(System::Guid viewportId)
    -> Task<bool>^
  {
    EnsureSurfaceRegistry();
    auto registry = GetSurfaceRegistry();
    auto key = ToGuidKey(viewportId);

    // Create the TaskCompletionSource and store it so we can resolve it when
    // the engine module processes the pending destruction.
    auto tcs = gcnew TaskCompletionSource<bool>(
      TaskCreationOptions::RunContinuationsAsynchronously);

    // Store the TaskCompletionSource in the native tokens_map keyed by the
    // native GuidKey (array of bytes). Use tokens_mutex for thread safety.
    TokenKey nativeKey{};
    for (size_t i = 0; i < nativeKey.size(); ++i) {
      nativeKey[i] = key[i];
    }

    // Create a native callback that resolves the stored TaskCompletionSource
    // when the engine module processes the pending destruction. Use the
    // MakeResolveCallback helper (outside the managed member function) so
    // no local class is defined inside this managed method.
    std::function<void(bool)> cb = MakeResolveCallback(nativeKey);

    try {
      auto msg = fmt::format(
        "UnregisterSurfaceAsync: stored token for viewport={}",
        msclr::interop::marshal_as<std::string>(viewportId.ToString()));
      LogInfoMessage(msg.c_str());
    }
    catch (...) { /* swallow logging errors */
    }

    // Pin the managed TaskCompletionSource using a GCHandle and store the
    // IntPtr -> pointer value in the native map so callbacks can resolve it
    // without holding managed references. Keep hold of the IntPtr so we can
    // free it if staging into the registry fails.
    System::IntPtr ip = IntPtr::Zero;
    {
      auto gh = System::Runtime::InteropServices::GCHandle::Alloc(
        tcs, System::Runtime::InteropServices::GCHandleType::Normal);
      ip = System::Runtime::InteropServices::GCHandle::ToIntPtr(gh);
      std::lock_guard<std::mutex> lg(tokens_mutex);
      tokens_map[nativeKey] = ip.ToPointer();
    }

    try {
      auto msg = fmt::format(
        "UnregisterSurfaceAsync: stored token for viewport={}",
        msclr::interop::marshal_as<std::string>(viewportId.ToString()));
      LogInfoMessage(msg.c_str());
    }
    catch (...) { /* swallow logging errors */
    }

    // Stage the removal into the registry; callback will be invoked by the
    // engine module when it drains pending destructions. If staging fails we
    // must cleanup the pinned GCHandle and remove the entry from tokens_map
    // to avoid leaking.
    try {
      registry->RemoveSurface(key, std::move(cb));
      try {
        auto msg2 = fmt::format(
          "UnregisterSurfaceAsync: staged removal for viewport={}",
          msclr::interop::marshal_as<std::string>(viewportId.ToString()));
        LogInfoMessage(msg2.c_str());
      }
      catch (...) { /* ignore logging failures */
      }
    }
    catch (...) {
      // ensure the saved GCHandle is freed and token removed
      try {
        auto msg = fmt::format(
          "UnregisterSurfaceAsync: staging removal failed for viewport={}, "
          "cleaning up token.",
          msclr::interop::marshal_as<std::string>(viewportId.ToString()));
        LogInfoMessage(msg.c_str());
      }
      catch (...) { /* swallow */
      }
      std::lock_guard<std::mutex> lg(tokens_mutex);
      auto it = tokens_map.find(nativeKey);
      if (it != tokens_map.end()) {
        void* hv = it->second;
        if (hv != nullptr) {
          try {
            System::IntPtr stored(hv);
            auto gh =
              System::Runtime::InteropServices::GCHandle::FromIntPtr(stored);
            if (gh.IsAllocated)
              gh.Free();
          }
          catch (...) { /* swallow */
          }
        }
        tokens_map.erase(it);
      }

      // Fail the TaskCompletionSource so the caller does not hang
      try {
        tcs->TrySetResult(false);
      }
      catch (...) { /* swallow */
      }

      return tcs->Task;
    }

    return tcs->Task;
  }

  auto EngineRunner::TryResizeSurfaceAsync(System::Guid viewportId,
    System::UInt32 width,
    System::UInt32 height) -> Task<bool>^
  {
    if (width == 0 || height == 0) {
      return Task::FromResult<bool>(false);
    }

    EnsureSurfaceRegistry();
    auto registry = GetSurfaceRegistry();
    auto key = ToGuidKey(viewportId);
    auto surface = registry->FindSurface(key);
    if (!surface) {
      return Task::FromResult<bool>(false);
    }

    auto tcs = gcnew TaskCompletionSource<bool>(
      TaskCreationOptions::RunContinuationsAsynchronously);
    TokenKey nativeKey{};
    for (size_t i = 0; i < nativeKey.size(); ++i)
      nativeKey[i] = key[i];
    System::IntPtr ip = IntPtr::Zero;
    {
      auto gh = System::Runtime::InteropServices::GCHandle::Alloc(
        tcs, System::Runtime::InteropServices::GCHandleType::Normal);
      ip = System::Runtime::InteropServices::GCHandle::ToIntPtr(gh);
      std::lock_guard<std::mutex> lg(tokens_mutex);
      tokens_map[nativeKey] = ip.ToPointer();
    }

    std::function<void(bool)> cb = MakeResolveCallback(nativeKey);

    try {
      registry->RegisterResizeCallback(key, std::move(cb));
      try {
        auto msg = fmt::format(
          "ResizeSurfaceAsync: staged resize for viewport={} size={}x{}",
          msclr::interop::marshal_as<std::string>(viewportId.ToString()), width,
          height);
        LogInfoMessage(msg.c_str());
      }
      catch (...) { /* swallow */
      }
    }
    catch (...) {
      // cleanup pinned handle + native entry if registration fails
      std::lock_guard<std::mutex> lg(tokens_mutex);
      auto it = tokens_map.find(nativeKey);
      if (it != tokens_map.end()) {
        void* hv = it->second;
        if (hv != nullptr) {
          try {
            System::IntPtr stored(hv);
            auto gh =
              System::Runtime::InteropServices::GCHandle::FromIntPtr(stored);
            if (gh.IsAllocated)
              gh.Free();
          }
          catch (...) { /* swallow */
          }
        }
        tokens_map.erase(it);
      }

      try {
        tcs->TrySetResult(false);
      }
      catch (...) { /* swallow */
      }
      return tcs->Task;
    }

    // Request the resize (mark-only). Engine module will pick this up and
    // perform the actual Resize() on next frame.
    oxygen::engine::interop::RequestCompositionSurfaceResize(surface, width,
      height);

    return tcs->Task;
  }

  auto EngineRunner::TryRegisterSurfaceAsync(
    EngineContext^ ctx, System::Guid documentId, System::Guid viewportId,
    System::String^ displayName, System::IntPtr swapChainPanel,
    System::UInt32 initialWidth, System::UInt32 initialHeight,
    float compositionScale) -> Task<bool>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (swapChainPanel == IntPtr::Zero) {
      throw gcnew ArgumentException("SwapChainPanel pointer must not be zero.",
        "swapChainPanel");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->VerifyAccess(
      gcnew String(L"RegisterSurfaceAsync requires the UI thread. "
        L"Call CreateEngine() on the UI thread first."));

    auto& shared = ctx->NativeShared();
    if (!shared) {
      return Task::FromResult<bool>(false);
    }

    EnsureSurfaceRegistry();
    auto registry = GetSurfaceRegistry();
    auto key = ToGuidKey(viewportId);

    auto docString = documentId.ToString();
    auto viewportString = viewportId.ToString();
    auto displayLabel = displayName != nullptr
      ? displayName
      : gcnew String(L"(unnamed viewport)");
    const auto doc = msclr::interop::marshal_as<std::string>(docString);
    const auto view = msclr::interop::marshal_as<std::string>(viewportString);
    const auto disp = msclr::interop::marshal_as<std::string>(displayLabel);

    try {
      auto registrationLog = fmt::format(
        "RegisterSurfaceAsync doc={} viewport={} name='{}'", doc, view, disp);
      LogInfoMessage(registrationLog.c_str());
    }
    catch (...) {
      LogInfoMessage("RegisterSurfaceAsync: failed to format timestamped log");
    }

    LogInfoMessage("RegisterSurfaceAsync: creating composition surface.");
    void* swap_chain_ptr = nullptr;
    auto surface = oxygen::engine::interop::CreateCompositionSurface(
      shared, &swap_chain_ptr);
    if (!surface) {
      LogInfoMessage(
        "RegisterSurfaceAsync failed: CreateCompositionSurface returned null.");
      return Task::FromResult<bool>(false);
    }

    try {
      surface->SetName(disp);
    }
    catch (...) { /* best-effort naming; ignore failures */
    }

    // Prepare the completion token and store in native token map so the
    // engine module can resolve it when the queued registration is processed.
    auto tcs = gcnew TaskCompletionSource<bool>(
      TaskCreationOptions::RunContinuationsAsynchronously);

    TokenKey nativeKey{};
    for (size_t i = 0; i < nativeKey.size(); ++i)
      nativeKey[i] = key[i];

    System::IntPtr ip = IntPtr::Zero;
    {
      auto gh = System::Runtime::InteropServices::GCHandle::Alloc(
        tcs, System::Runtime::InteropServices::GCHandleType::Normal);
      ip = System::Runtime::InteropServices::GCHandle::ToIntPtr(gh);
      std::lock_guard<std::mutex> lg(tokens_mutex);
      tokens_map[nativeKey] = ip.ToPointer();
    }

    std::function<void(bool)> cb = MakeResolveCallback(nativeKey);

    try {
      registry->RegisterSurface(key, surface, std::move(cb));
      try {
        auto msg = fmt::format(
          "RegisterSurfaceAsync: staged registration for viewport={}",
          msclr::interop::marshal_as<std::string>(viewportId.ToString()));
        LogInfoMessage(msg.c_str());
      }
      catch (...) { /* swallow logging failures */
      }
    }
    catch (...) {
      // cleanup pinned handle + native entry if staging fails
      std::lock_guard<std::mutex> lg(tokens_mutex);
      auto it = tokens_map.find(nativeKey);
      if (it != tokens_map.end()) {
        void* hv = it->second;
        if (hv != nullptr) {
          try {
            System::IntPtr stored(hv);
            auto gh =
              System::Runtime::InteropServices::GCHandle::FromIntPtr(stored);
            if (gh.IsAllocated)
              gh.Free();
          }
          catch (...) { /* swallow */
          }
        }
        tokens_map.erase(it);
      }

      try {
        tcs->TrySetResult(false);
      }
      catch (...) { /* swallow */
      }
      return tcs->Task;
    }

    if (swap_chain_ptr != nullptr) {
      auto surface_ptr = new std::shared_ptr<oxygen::graphics::Surface>(surface);
      AttachSwapChain(swapChainPanel, IntPtr(swap_chain_ptr), IntPtr(surface_ptr),
        compositionScale);
    }

    // If the caller supplied an initial desired size, request a staged resize
    // here so the composition surface will be resized (and native backbuffers
    // created) prior to the engine processing the registration on the next
    // frame. This avoids the initial 1x1 default remaining as the backbuffer
    // when the panel already reports a measurable size.
    if (initialWidth > 0 && initialHeight > 0) {
      try {
        oxygen::engine::interop::RequestCompositionSurfaceResize(
          surface, static_cast<uint32_t>(initialWidth),
          static_cast<uint32_t>(initialHeight));
        try {
          auto msg = fmt::format(
            "RegisterSurfaceAsync: requested initial resize for viewport={} "
            "size={}x{}",
            msclr::interop::marshal_as<std::string>(viewportId.ToString()),
            initialWidth, initialHeight);
          LogInfoMessage(msg.c_str());
        }
        catch (...) { /* swallow logging failures */
        }
      }
      catch (...) {
        /* best-effort: ignore resize request errors here */
      }
    }

    return tcs->Task;
  }

  void EngineRunner::ResetSurfaceRegistry() {
    if (surface_registry_ != nullptr && surface_registry_->get() != nullptr) {
      (*surface_registry_)->Clear();
    }
  }

  void EngineRunner::EnsureSurfaceRegistry() {
    if (surface_registry_ == nullptr) {
      surface_registry_ = new std::shared_ptr<SurfaceRegistry>(
        std::make_shared<SurfaceRegistry>());
      return;
    }

    if (surface_registry_->get() == nullptr) {
      *surface_registry_ = std::make_shared<SurfaceRegistry>();
    }
  }

  auto EngineRunner::GetSurfaceRegistry() -> std::shared_ptr<SurfaceRegistry> {
    EnsureSurfaceRegistry();
    return *surface_registry_;
  }

  auto EngineRunner::ToGuidKey(System::Guid guid) -> SurfaceRegistry::GuidKey {
    SurfaceRegistry::GuidKey key{};
    auto bytes = guid.ToByteArray();
    if (bytes == nullptr || bytes->Length != 16) {
      return key;
    }

    for (int i = 0; i < 16; ++i) {
      key[static_cast<std::size_t>(i)] = bytes[i];
    }

    return key;
  }

  void EngineRunner::AttachSwapChain(IntPtr panelPtr, IntPtr swapChainPtr,
    IntPtr surfaceHandle,
    float compositionScale) {
    if (panelPtr == IntPtr::Zero || swapChainPtr == IntPtr::Zero) {
      return;
    }

    auto state = gcnew SwapChainAttachState(panelPtr, swapChainPtr, surfaceHandle,
      compositionScale);
    if (ui_dispatcher_ == nullptr || !ui_dispatcher_->IsCaptured) {
      throw gcnew InvalidOperationException(gcnew String(
        L"SwapChain attachment requires a captured UI SynchronizationContext. "
        L"Ensure CreateEngine() was called on the UI thread."));
    }

    ui_dispatcher_->Post(
      gcnew SendOrPostCallback(this, &EngineRunner::AttachSwapChainCallback),
      state);
  }

  void EngineRunner::AttachSwapChainCallback(Object^ state) {
    auto attachState = dynamic_cast<SwapChainAttachState^>(state);
    if (attachState == nullptr) {
      return;
    }

    auto panelUnknown =
      reinterpret_cast<IUnknown*>(attachState->PanelPtr.ToPointer());
    auto swapChain =
      reinterpret_cast<IDXGISwapChain*>(attachState->SwapChainPtr.ToPointer());
    auto surfaceHandlePtr =
      reinterpret_cast<std::shared_ptr<oxygen::graphics::Surface> *>(
        attachState->SurfaceHandle.ToPointer());
    // Log the incoming attach with timestamp and surface reference info (if
    // provided).
    try {
      auto attachLog =
        fmt::format("AttachSwapChainCallback: panel={} swapchain={}",
          fmt::ptr(panelUnknown), fmt::ptr(swapChain));
      if (surfaceHandlePtr != nullptr) {
        attachLog += fmt::format(" surface_handle_ptr={} use_count={}",
          fmt::ptr(surfaceHandlePtr),
          surfaceHandlePtr->use_count());
      }
      LogInfoMessage(attachLog.c_str());
    }
    catch (...) {
      LogInfoMessage(
        "AttachSwapChainCallback: failed to format attach diagnostics.");
    }
    if (panelUnknown == nullptr || swapChain == nullptr) {
      return;
    }

    ISwapChainPanelNative* panelNative = nullptr;
    HRESULT hr = panelUnknown->QueryInterface(
      __uuidof(ISwapChainPanelNative), reinterpret_cast<void**>(&panelNative));
    if (FAILED(hr) || panelNative == nullptr) {
      LogInfoMessage(
        "Failed to acquire ISwapChainPanelNative from SwapChainPanel.");
      return;
    }

    hr = panelNative->SetSwapChain(swapChain);
    panelNative->Release();
    if (FAILED(hr)) {
      LogInfoMessage("ISwapChainPanelNative::SetSwapChain failed.");
      // cleanup surface handle if present
      if (surfaceHandlePtr != nullptr) {
        try {
          auto errLog = fmt::format(
            "AttachSwapChainCallback: SetSwapChain failed, cleaning "
            "surface_handle_ptr={} pre-delete use_count={}",
            fmt::ptr(surfaceHandlePtr), surfaceHandlePtr->use_count());
          LogInfoMessage(errLog.c_str());
        }
        catch (...) {
          LogInfoMessage("AttachSwapChainCallback: error logging before delete.");
        }
        delete surfaceHandlePtr;
      }
      return;
    }

    LogInfoMessage("SwapChain attached to panel.");

    // Apply inverse scale transform to counteract SwapChainPanel's automatic DPI
    // scaling.
    //
    // CRITICAL FIX FOR HIGH DPI SCREENS (Issue #8219):
    // WinUI's SwapChainPanel automatically applies a scale transform based on the
    // CompositionScale (DPI scale) to the content. When rendering at full
    // physical resolution (1:1 pixel mapping), this automatic scaling causes the
    // content to be "zoomed in" and truncated/cropped at the bottom-right.
    //
    // To fix this, we must apply an INVERSE scale transform to the SwapChain
    // itself. This cancels out the compositor's scaling, ensuring that our 1:1
    // rendered pixels map exactly to the physical screen pixels without being
    // stretched or cropped.
    if (attachState->CompositionScale > 0.0f) {
      IDXGISwapChain2* swapChain2 = nullptr;
      HRESULT hr2 = swapChain->QueryInterface(
        __uuidof(IDXGISwapChain2), reinterpret_cast<void**>(&swapChain2));
      if (SUCCEEDED(hr2) && swapChain2 != nullptr) {
        DXGI_MATRIX_3X2_F inverseScale = {};
        inverseScale._11 = 1.0f / attachState->CompositionScale;
        inverseScale._22 = 1.0f / attachState->CompositionScale;
        swapChain2->SetMatrixTransform(&inverseScale);
        swapChain2->Release();
        LogInfoMessage("Applied inverse scale transform to SwapChain.");
      }
      else {
        LogInfoMessage(
          "Failed to query IDXGISwapChain2 for inverse scale transform.");
      }
    }

    // if we received a temporary owning pointer, drop it now to return ownership
    // to the registry/engine. We intentionally log the use_count for diagnostics
    // before deleting the heap-held shared_ptr.
    if (surfaceHandlePtr != nullptr) {
      try {
        auto cleanupLog = fmt::format(
          "AttachSwapChainCallback cleaning surface_handle_ptr={} pre-delete "
          "use_count={}",
          fmt::ptr(surfaceHandlePtr), surfaceHandlePtr->use_count());
        LogInfoMessage(cleanupLog.c_str());
      }
      catch (...) {
        LogInfoMessage("AttachSwapChainCallback: error logging cleanup info.");
      }
      delete surfaceHandlePtr;
    }
  }

} // namespace Oxygen::Interop
