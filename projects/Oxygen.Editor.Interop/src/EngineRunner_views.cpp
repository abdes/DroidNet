//===----------------------------------------------------------------------===//
// EngineRunner - view management implementation
// Separated to a dedicated compilation unit for clarity.
//===----------------------------------------------------------------------===//

#pragma managed

#include <functional>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <msclr/marshal_cppstd.h>

#include <Oxygen/Engine/AsyncEngine.h>
#include "EditorModule/EditorModule.h"
#include "EngineRunner.h"

#include "Views/ViewIdManaged.h"
#include "Views/ViewConfigManaged.h"

using namespace System;
using namespace System::Threading::Tasks;
using namespace msclr::interop;

namespace {

  // File-scope helper that resolves a pinned TaskCompletionSource and sets
  // the ViewIdManaged result. Kept in this file to avoid local lambda types
  // inside managed member functions.
  static void ResolveCreateViewCallback(void* handlePtr, bool ok,
    ::oxygen::ViewId nativeId) {
        using namespace Oxygen::Interop;
    try {
      System::IntPtr stored(handlePtr);
      auto gh = System::Runtime::InteropServices::GCHandle::FromIntPtr(stored);
      auto tcsObj = safe_cast<TaskCompletionSource<ViewIdManaged>^>(gh.Target);
      if (tcsObj != nullptr) {
        if (ok) {
          auto vm = ViewIdManaged::FromNative(nativeId);
          tcsObj->TrySetResult(vm);
        }
        else {
          tcsObj->TrySetResult(ViewIdManaged::Invalid);
        }
      }
      if (gh.IsAllocated) gh.Free();
    }
    catch (...) {
      /* swallow */
    }
  }

} // anonymous namespace

namespace Oxygen::Interop {

  auto EngineRunner::CreateViewAsync(EngineContext^ ctx, ViewConfigManaged^ cfg)
    -> System::Threading::Tasks::Task<ViewIdManaged>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (cfg == nullptr) {
      throw gcnew ArgumentNullException("cfg");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    // Calls to create views must originate from the UI thread.
    ui_dispatcher_->VerifyAccess(
      gcnew String(L"CreateViewAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task<ViewIdManaged>::FromResult(ViewIdManaged::Invalid);
    }

    // Convert managed config to native EditorView::Config
    oxygen::interop::module::EditorView::Config native_cfg;
    native_cfg.name = marshal_as<std::string>(cfg->Name);
    native_cfg.purpose = marshal_as<std::string>(cfg->Purpose);
    native_cfg.width = static_cast<uint32_t>(cfg->Width);
    native_cfg.height = static_cast<uint32_t>(cfg->Height);
    native_cfg.clear_color = cfg->ClearColor.ToNative();

    // If caller supplied a compositing target GUID, try to resolve to a
    // native surface pointer via the surface registry.
    if (cfg->CompositingTarget.HasValue) {
      auto key = ToGuidKey(cfg->CompositingTarget.Value);
      auto registry = GetSurfaceRegistry();
      auto surface = registry->FindSurface(key);
      if (surface) {
        native_cfg.compositing_target = surface.get();
      }
    }

    // Prepare TaskCompletionSource for ViewIdManaged result and pin it.
    auto tcs = gcnew TaskCompletionSource<ViewIdManaged>(
      TaskCreationOptions::RunContinuationsAsynchronously);

    // Pin the TaskCompletionSource with a GCHandle so the native callback
    // can resolve it later from the engine thread without holding managed refs.
    System::IntPtr ip = System::IntPtr::Zero;
    {
      auto gh = System::Runtime::InteropServices::GCHandle::Alloc(
        tcs, System::Runtime::InteropServices::GCHandleType::Normal);
      ip = System::Runtime::InteropServices::GCHandle::ToIntPtr(gh);
    }

    void* handlePtr = ip.ToPointer();

    // Use file-scope resolver callback
    std::function<void(bool, ::oxygen::ViewId)> cb =
      std::bind(&ResolveCreateViewCallback, handlePtr, std::placeholders::_1, std::placeholders::_2);

    // Find the EditorModule and forward the request
    auto native_ctx_shared = ctx->NativePtr();
    auto editor_module_opt = native_ctx_shared->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      // Fail fast: free handle and return invalid id
      try {
        System::IntPtr stored(handlePtr);
        auto gh = System::Runtime::InteropServices::GCHandle::FromIntPtr(stored);
        if (gh.IsAllocated) gh.Free();
      }
      catch (...) {}
      tcs->TrySetResult(ViewIdManaged::Invalid);
      return tcs->Task;
    }

    // Forward to editor module (this enqueues into the engine thread and will
    // invoke our callback on the engine thread when processed)
    try {
      editor_module_opt->get().CreateViewAsync(native_cfg, std::move(cb));
    }
    catch (...) {
      // make sure we free handle if forwarding failed
      try {
        System::IntPtr stored(handlePtr);
        auto gh = System::Runtime::InteropServices::GCHandle::FromIntPtr(stored);
        if (gh.IsAllocated) gh.Free();
      }
      catch (...) {}
      tcs->TrySetResult(ViewIdManaged::Invalid);
    }

    return tcs->Task;
  }

} // namespace Oxygen::Interop
