//===----------------------------------------------------------------------===//
// EngineRunner - view management implementation
// Separated to a dedicated compilation unit for clarity.
//===----------------------------------------------------------------------===//

#pragma managed

#include <cstdint>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#ifdef _WIN32
#include <WinSock2.h> // include before any header that might include <windows.h>
#endif

#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/EditorInterface/EngineContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Types/Color.h>

#include <EditorModule/EditorModule.h>
#include <EngineRunner.h>
#include <Views/CameraControlModeManaged.h>
#include <Views/CameraViewPresetManaged.h>
#include <Views/EditorCameraStateManaged.h>
#include <Views/ViewConfigManaged.h>
#include <Views/ViewIdManaged.h>

using namespace System;
using namespace System::Threading::Tasks;
using namespace msclr::interop;

namespace {

  // Resolves a pinned TaskCompletionSource with a view camera pose, or null.
  static void ResolveViewCameraPoseCallback(void* handlePtr,
    std::optional<::oxygen::interop::module::EditorCameraPose> pose) {
    using namespace Oxygen::Interop;
    try {
      System::IntPtr stored(handlePtr);
      auto gh = System::Runtime::InteropServices::GCHandle::FromIntPtr(stored);
      auto tcsObj
        = safe_cast<TaskCompletionSource<ViewCameraPoseManaged^>^>(gh.Target);
      if (tcsObj != nullptr) {
        ViewCameraPoseManaged^ result = nullptr;
        if (pose.has_value()) {
          result = gcnew ViewCameraPoseManaged();
          result->Position = System::Numerics::Vector3(
            pose->position.x, pose->position.y, pose->position.z);
          result->RotationDegrees = System::Numerics::Vector3(
            pose->rotation_degrees.x, pose->rotation_degrees.y,
            pose->rotation_degrees.z);
          result->Scale = System::Numerics::Vector3(
            pose->scale.x, pose->scale.y, pose->scale.z);
          result->OrthographicSize = pose->orthographic_size.has_value()
            ? System::Nullable<float>(*pose->orthographic_size)
            : System::Nullable<float>();
          result->FieldOfViewDegrees = pose->field_of_view_degrees.has_value()
            ? System::Nullable<float>(*pose->field_of_view_degrees)
            : System::Nullable<float>();
        }
        tcsObj->TrySetResult(result);
      }
      if (gh.IsAllocated) gh.Free();
    }
    catch (...) {
      // Swallow: the native caller must not observe managed exceptions.
    }
  }

  // Resolves a pinned TaskCompletionSource with an editor camera state, or null.
  static void ResolveViewEditorCameraCallback(void* handlePtr,
    std::optional<::oxygen::interop::module::EditorCameraState> state) {
    using namespace Oxygen::Interop;
    try {
      System::IntPtr stored(handlePtr);
      auto gh = System::Runtime::InteropServices::GCHandle::FromIntPtr(stored);
      auto tcsObj
        = safe_cast<TaskCompletionSource<EditorCameraStateManaged^>^>(gh.Target);
      if (tcsObj != nullptr) {
        tcsObj->TrySetResult(state.has_value()
            ? EditorCameraStateManaged::FromNative(*state)
            : nullptr);
      }
      if (gh.IsAllocated) gh.Free();
    }
    catch (...) {
      // Swallow: the native caller must not observe managed exceptions.
    }
  }

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

  namespace {

    [[nodiscard]] auto ToNativeCameraControlMode(CameraControlModeManaged mode)
      -> ::oxygen::interop::module::EditorViewportCameraControlMode {
      using NativeMode =
        ::oxygen::interop::module::EditorViewportCameraControlMode;
      switch (mode) {
      case CameraControlModeManaged::OrbitTrackball:
        return NativeMode::kOrbitTrackball;
      case CameraControlModeManaged::Fly:
        return NativeMode::kFly;
      case CameraControlModeManaged::OrbitTurntable:
      default:
        return NativeMode::kOrbitTurntable;
      }
    }

  } // namespace

  auto EngineRunner::TryCreateViewAsync(EngineContext^ ctx, ViewConfigManaged^ cfg)
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

    auto native_cfg = cfg->ToNative();
    // Defensive: if managed caller still supplied an empty string, set a
    // clear fallback name so native logs are useful for debugging.
    if (native_cfg.name.empty()) {
      native_cfg.name = "EditorView:Unnamed";
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

  auto EngineRunner::TryDestroyViewAsync(EngineContext^ ctx, ViewIdManaged viewId)
    -> System::Threading::Tasks::Task<bool>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    // Ensure called from UI thread
    ui_dispatcher_->VerifyAccess(
      gcnew String(L"DestroyViewAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    auto nativeId = viewId.ToNative();

    // Find the EditorModule
    auto editor_module_opt = native_ctx->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    try {
      // Forward destroy request to the EditorModule (synchronous)
      editor_module_opt->get().DestroyView(nativeId);
      return System::Threading::Tasks::Task<bool>::FromResult(true);
    }
    catch (...) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }
  }

  auto EngineRunner::TryShowViewAsync(EngineContext^ ctx, ViewIdManaged viewId)
    -> System::Threading::Tasks::Task<bool>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    // Ensure called on UI thread
    ui_dispatcher_->VerifyAccess(
      gcnew String(L"ShowViewAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    auto nativeId = viewId.ToNative();

    // Find the EditorModule
    auto editor_module_opt = native_ctx->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    try {
      editor_module_opt->get().ShowView(nativeId);
      return System::Threading::Tasks::Task<bool>::FromResult(true);
    }
    catch (...) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }
  }

  auto EngineRunner::TryHideViewAsync(EngineContext^ ctx, ViewIdManaged viewId)
    -> System::Threading::Tasks::Task<bool>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    // Ensure called on UI thread
    ui_dispatcher_->VerifyAccess(
      gcnew String(L"HideViewAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    auto nativeId = viewId.ToNative();

    // Find the EditorModule
    auto editor_module_opt = native_ctx->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    try {
      editor_module_opt->get().HideView(nativeId);
      return System::Threading::Tasks::Task<bool>::FromResult(true);
    }
    catch (...) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }
  }

  auto EngineRunner::TrySetViewCameraPresetAsync(EngineContext^ ctx,
    ViewIdManaged viewId, CameraViewPresetManaged preset)
    -> System::Threading::Tasks::Task<bool>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->VerifyAccess(
      gcnew String(L"SetViewCameraPresetAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    auto nativeId = viewId.ToNative();

    auto editor_module_opt =
      native_ctx->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    try {
      const auto native_preset = ToNativeCameraViewPreset(preset);
      editor_module_opt->get().SetViewCameraPreset(nativeId, native_preset);
      return System::Threading::Tasks::Task<bool>::FromResult(true);
    }
    catch (...) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }
  }

  auto EngineRunner::TrySetViewSceneCameraAsync(EngineContext^ ctx,
    ViewIdManaged viewId, System::Guid cameraNodeId)
    -> System::Threading::Tasks::Task<bool>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->VerifyAccess(
      gcnew String(L"SetViewSceneCameraAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    auto editor_module_opt =
      native_ctx->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    std::optional<oxygen::interop::module::UuidKey> node_key;
    if (cameraNodeId != System::Guid::Empty) {
      auto bytes = cameraNodeId.ToByteArray();
      oxygen::interop::module::UuidKey key {};
      for (int i = 0; i < 16; ++i) {
        key[i] = bytes[i];
      }
      node_key = key;
    }

    try {
      editor_module_opt->get().SetViewSceneCamera(viewId.ToNative(), node_key);
      return System::Threading::Tasks::Task<bool>::FromResult(true);
    }
    catch (...) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }
  }

  auto EngineRunner::TrySetViewScenePilotAsync(EngineContext^ ctx,
    ViewIdManaged viewId, bool pilot)
    -> System::Threading::Tasks::Task<bool>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->VerifyAccess(
      gcnew String(L"SetViewScenePilotAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    auto editor_module_opt =
      native_ctx->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    try {
      editor_module_opt->get().SetViewScenePilot(viewId.ToNative(), pilot);
      return System::Threading::Tasks::Task<bool>::FromResult(true);
    }
    catch (...) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }
  }

  auto EngineRunner::TryGetViewCameraPoseAsync(EngineContext^ ctx,
    ViewIdManaged viewId, System::Guid nodeId)
    -> System::Threading::Tasks::Task<ViewCameraPoseManaged^>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->VerifyAccess(
      gcnew String(L"GetViewCameraPoseAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task::FromResult<ViewCameraPoseManaged^>(nullptr);
    }

    auto editor_module_opt =
      native_ctx->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      return System::Threading::Tasks::Task::FromResult<ViewCameraPoseManaged^>(nullptr);
    }

    auto bytes = nodeId.ToByteArray();
    oxygen::interop::module::UuidKey key {};
    for (int i = 0; i < 16; ++i) {
      key[i] = bytes[i];
    }

    auto tcs = gcnew TaskCompletionSource<ViewCameraPoseManaged^>(
      TaskCreationOptions::RunContinuationsAsynchronously);
    auto gh = System::Runtime::InteropServices::GCHandle::Alloc(
      tcs, System::Runtime::InteropServices::GCHandleType::Normal);
    void* handlePtr = System::Runtime::InteropServices::GCHandle::ToIntPtr(gh).ToPointer();

    try {
      // The command answers exactly once, also when it is dropped unexecuted,
      // which frees the handle.
      editor_module_opt->get().QueryViewCameraPose(viewId.ToNative(), key,
        std::bind(&ResolveViewCameraPoseCallback, handlePtr,
          std::placeholders::_1));
    }
    catch (...) {
      ResolveViewCameraPoseCallback(handlePtr, std::nullopt);
    }
    return tcs->Task;
  }

  auto EngineRunner::TryGetViewEditorCameraAsync(EngineContext^ ctx,
    ViewIdManaged viewId)
    -> System::Threading::Tasks::Task<EditorCameraStateManaged^>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->VerifyAccess(
      gcnew String(L"GetViewEditorCameraAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task::FromResult<EditorCameraStateManaged^>(nullptr);
    }

    auto editor_module_opt =
      native_ctx->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      return System::Threading::Tasks::Task::FromResult<EditorCameraStateManaged^>(nullptr);
    }

    auto tcs = gcnew TaskCompletionSource<EditorCameraStateManaged^>(
      TaskCreationOptions::RunContinuationsAsynchronously);
    auto gh = System::Runtime::InteropServices::GCHandle::Alloc(
      tcs, System::Runtime::InteropServices::GCHandleType::Normal);
    void* handlePtr = System::Runtime::InteropServices::GCHandle::ToIntPtr(gh).ToPointer();

    try {
      // The command answers exactly once, also when it is dropped unexecuted,
      // which frees the handle.
      editor_module_opt->get().QueryViewEditorCamera(viewId.ToNative(),
        std::bind(&ResolveViewEditorCameraCallback, handlePtr,
          std::placeholders::_1));
    }
    catch (...) {
      ResolveViewEditorCameraCallback(handlePtr, std::nullopt);
    }
    return tcs->Task;
  }

  auto EngineRunner::TrySetViewCameraControlModeAsync(EngineContext^ ctx,
    ViewIdManaged viewId,
    CameraControlModeManaged mode)
    -> System::Threading::Tasks::Task<bool>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->VerifyAccess(
      gcnew String(L"SetViewCameraControlModeAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    auto nativeId = viewId.ToNative();

    auto editor_module_opt =
      native_ctx->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    try {
      const auto native_mode = ToNativeCameraControlMode(mode);
      editor_module_opt->get().SetViewCameraControlMode(nativeId, native_mode);
      return System::Threading::Tasks::Task<bool>::FromResult(true);
    }
    catch (...) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }
  }

  auto EngineRunner::TrySetViewCameraMovementSpeedAsync(
    EngineContext^ ctx,
    ViewIdManaged viewId,
    float speedUnitsPerSecond)
    -> System::Threading::Tasks::Task<bool>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->VerifyAccess(
      gcnew String(L"SetViewCameraMovementSpeedAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    auto nativeId = viewId.ToNative();

    auto editor_module_opt =
      native_ctx->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    try {
      editor_module_opt->get().SetViewCameraMovementSpeed(
        nativeId, speedUnitsPerSecond);
      return System::Threading::Tasks::Task<bool>::FromResult(true);
    }
    catch (...) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }
  }

  auto EngineRunner::TrySetViewCameraSettingsAsync(
    EngineContext^ ctx,
    ViewIdManaged viewId,
    float fieldOfViewDegrees,
    float nearPlane,
    float farPlane)
    -> System::Threading::Tasks::Task<bool>^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->VerifyAccess(
      gcnew String(L"SetViewCameraSettingsAsync requires the UI thread. Call CreateEngine() on the UI thread first."));

    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    auto nativeId = viewId.ToNative();

    auto editor_module_opt =
      native_ctx->engine->GetModule<oxygen::interop::module::EditorModule>();
    if (!editor_module_opt) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }

    try {
      constexpr float degrees_to_radians = 0.01745329251994329576923690768489F;
      editor_module_opt->get().SetViewCameraSettings(nativeId,
        fieldOfViewDegrees * degrees_to_radians, nearPlane, farPlane);
      return System::Threading::Tasks::Task<bool>::FromResult(true);
    }
    catch (...) {
      return System::Threading::Tasks::Task<bool>::FromResult(false);
    }
  }

} // namespace Oxygen::Interop
