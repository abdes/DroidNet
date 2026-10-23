//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch_clr.h"

#pragma managed(push, off)
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/fwd.hpp>

#include <Oxygen/EditorInterface/EngineContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Data/LodPolicyValidation.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <Commands/DetachGeometryCommand.h>
#include <Commands/LocalFogVolumeCommands.h>
#include <Commands/PropertyApplierRegistry.h>
#include <Commands/RemoveSceneNodeCommand.h>
#include <Commands/RemoveSceneNodesCommand.h>
#include <Commands/RenameSceneNodeCommand.h>
#include <Commands/ReparentSceneNodeCommand.h>
#include <Commands/ReparentSceneNodesCommand.h>
#include <Commands/SetEnvironmentCommand.h>
#include <Commands/SetGeometryCommand.h>
#include <Commands/SetLocalTransformCommand.h>
#include <Commands/SetOverridesCommand.h>
#include <Commands/SchemaEnums.h>
#include <Commands/SetPropertiesCommand.h>
#include <Commands/UpdateTransformsForNodesCommand.h>
#include <EditorModule/EditorModule.h>
#include <EditorModule/NodeRegistry.h>
#include <EditorModule/SceneAssetRequests.h>
#pragma managed(pop)

#pragma managed

#include <msclr/gcroot.h>
#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include <Commands/AttachCameraCommand.h>
#include <Commands/AttachLightCommand.h>
#include <Commands/CreateSceneNodeCommand.h>
#include <EditorModule/CommandFactory.h>
#include <World/FogEnvironmentConversion.h>
#include <World/OxygenWorld.h>
#include <World/SkyEnvironmentConversion.h>
#include <Utils/Utf8Marshaling.h>

using namespace System::Threading::Tasks;

using namespace oxygen::interop::module;
using namespace oxygen::interop::module;

namespace Oxygen::Interop::World {

  namespace {
    class CookedRootsCompletion final {
    public:
      explicit CookedRootsCompletion(TaskCompletionSource<bool>^ completion)
        : completion_(completion) {}
      ~CookedRootsCompletion() { completion_->TrySetCanceled(); }
      void Complete(bool succeeded, const std::string& error) const {
        if (succeeded) {
          completion_->TrySetResult(true);
        } else {
          completion_->TrySetException(gcnew System::InvalidOperationException(gcnew System::String(error.c_str())));
        }
      }
    private:
      msclr::gcroot<TaskCompletionSource<bool>^> completion_;
    };

    static std::function<void(bool, std::string)> MakeCookedRootsCallback(
      TaskCompletionSource<bool>^ completion) {
      auto owner = std::shared_ptr<CookedRootsCompletion>(
        new CookedRootsCompletion(completion));
      return [owner](bool succeeded, std::string error) {
        owner->Complete(succeeded, error);
      };
    }

    static System::Threading::Tasks::Task<bool>^ CreateFailedBoolTask(
      const char* message)
    {
      auto tcs = gcnew System::Threading::Tasks::TaskCompletionSource<bool>();
      tcs->SetException(gcnew System::InvalidOperationException(
        gcnew System::String(message)));
      return tcs->Task;
    }

    // Helper to build a native completion callback that posts into a managed
    // TaskCompletionSource. The pointer is owned by the callback and deleted
    // after invocation to avoid leaks.
    static std::function<void(bool, std::string)> MakeTcsCallback(
      msclr::gcroot<System::Threading::Tasks::TaskCompletionSource<bool>^>* ptr)
    {
      return [ptr](bool ok, std::string error) mutable {
        try {
          if (ok) {
            (*ptr)->SetResult(true);
          } else {
            auto msg = gcnew System::String(error.c_str());
            (*ptr)->SetException(gcnew System::InvalidOperationException(msg));
          }
        }
        catch (...) {
          // swallow
        }
        delete ptr;
        };
    }

    class AssetFailureObserver final {
    public:
      explicit AssetFailureObserver(Action<System::UInt64, String^>^ callback)
        : callback_(callback) {}

      void Invoke(uint64_t generation, const std::string& message) const {
        try {
          callback_->Invoke(generation, gcnew String(message.c_str()));
        } catch (System::Exception^ exception) {
          System::Diagnostics::Trace::TraceError(
            "Runtime asset failure observer threw: " + exception->Message);
        }
      }

    private:
      msclr::gcroot<Action<System::UInt64, String^>^> callback_;
    };

    class AssetSuccessObserver final {
    public:
      explicit AssetSuccessObserver(Action<System::UInt64>^ callback)
        : callback_(callback) {}

      void Invoke(uint64_t generation) const {
        try {
          callback_->Invoke(generation);
        } catch (System::Exception^ exception) {
          System::Diagnostics::Trace::TraceError(
            "Runtime asset success observer threw: " + exception->Message);
        }
      }

    private:
      msclr::gcroot<Action<System::UInt64>^> callback_;
    };

    static SceneAssetRequests::SuccessCallback MakeAssetSuccessCallback(
      Action<System::UInt64>^ on_success) {
      if (on_success == nullptr) {
        return {};
      }
      auto callback = std::shared_ptr<AssetSuccessObserver>(
        new AssetSuccessObserver(on_success));
      return [callback](uint64_t generation) { callback->Invoke(generation); };
    }

    // Keep CLR types out of constrained shared_ptr forwarding constructors.
    // The native owner releases its gcroot on supersession or scene destruction.
    static SceneAssetRequests::FailureCallback MakeAssetFailureCallback(
      Action<System::UInt64, String^>^ on_failure) {
      if (on_failure == nullptr) {
        throw gcnew ArgumentNullException("onFailure");
      }
      auto callback = std::shared_ptr<AssetFailureObserver>(
        new AssetFailureObserver(on_failure));
      return [callback](uint64_t generation, const std::string& message) {
        callback->Invoke(generation, message);
      };
    }

    namespace interop = oxygen::interop::module;

    //! The native vector of a managed one.
    static auto ToVec3(System::Numerics::Vector3 value) -> oxygen::Vec3
    {
      return { value.X, value.Y, value.Z };
    }

    //! The common light values of the managed attach arguments; a light
    //! mobility other than Realtime is refused.
    static auto ToLightCommonValues(System::Numerics::Vector3 color,
      bool affectsWorld, int mobility, bool castsShadows, float shadowBias,
      float shadowNormalBias, bool contactShadows, int shadowResolutionHint,
      float exposureCompensation) -> interop::LightCommonValues
    {
      if (mobility != 0) {
        throw gcnew System::ArgumentOutOfRangeException("light", "Unsupported light mobility");
      }
      return {
        .color = ToVec3(color),
        .affects_world = affectsWorld,
        .casts_shadows = castsShadows,
        .shadow_bias = shadowBias,
        .shadow_normal_bias = shadowNormalBias,
        .contact_shadows = contactShadows,
        .shadow_resolution_hint = shadowResolutionHint,
        .exposure_compensation = exposureCompensation,
      };
    }

    struct NodeCommandTarget {
      EditorModule& module;
      oxygen::scene::NodeHandle node;
    };

    //! Resolves the editor module and a registered node, or nothing when
    //! either is unavailable.
    static auto LookupNodeForCommand(EngineContext^ context,
      System::Guid nodeId) -> std::optional<NodeCommandTarget>
    {
      auto native_ctx = context->NativePtr();
      if (!native_ctx || !native_ctx->engine) {
        return std::nullopt;
      }
      auto editor_module = native_ctx->engine->GetModule<EditorModule>();
      if (!editor_module) {
        return std::nullopt;
      }
      auto bytes = nodeId.ToByteArray();
      std::array<uint8_t, 16> key {};
      for (int i = 0; i < 16; ++i) {
        key[i] = bytes[i];
      }
      const auto handle = NodeRegistry::Lookup(key);
      if (!handle) {
        return std::nullopt;
      }
      return NodeCommandTarget { .module = editor_module->get(), .node = *handle };
    }

    //! Resolves the editor module and a registered node for a component
    //! attach, reporting either one missing as `SetProperties` does.
    static auto RequireNodeForCommand(EngineContext^ context,
      System::Guid nodeId, System::String^ operation) -> NodeCommandTarget
    {
      auto native_ctx = context->NativePtr();
      if (!native_ctx || !native_ctx->engine) {
        throw gcnew System::InvalidOperationException(operation
          + " failed because the native engine context is not available.");
      }
      auto editor_module = native_ctx->engine->GetModule<EditorModule>();
      if (!editor_module) {
        throw gcnew System::InvalidOperationException(
          operation + " failed because EditorModule is not available.");
      }
      const auto target = LookupNodeForCommand(context, nodeId);
      if (!target) {
        throw gcnew System::ArgumentException(operation
          + " failed because the target scene node is not registered.");
      }
      return *target;
    }

    //! Enqueues attaching `light`, which `MakeDirectionalLight`,
    //! `MakePointLight` or `MakeSpotLight` built and validated.
    static void EnqueueAttachLight(EngineContext^ context, System::Guid nodeId,
      std::unique_ptr<oxygen::Component> light)
    {
      const auto target = RequireNodeForCommand(context, nodeId, "AttachLight");
      target.module.Enqueue(
        std::make_unique<AttachLightCommand>(target.node, std::move(light)));
    }

    static void EnqueueDetachLight(EngineContext^ context, System::Guid nodeId)
    {
      auto native_ctx = context->NativePtr();
      if (!native_ctx || !native_ctx->engine) {
        return;
      }

      auto editor_module = native_ctx->engine->GetModule<EditorModule>();
      if (!editor_module) {
        return;
      }

      auto b = nodeId.ToByteArray();
      std::array<uint8_t, 16> key {};
      for (int i = 0; i < 16; ++i) {
        key[i] = b[i];
      }

      auto opt = NodeRegistry::Lookup(key);
      if (!opt.has_value()) {
        return;
      }

      auto cmd = std::make_unique<DetachLightCommand>(opt.value());
      editor_module->get().Enqueue(std::move(cmd));
    }
  }

  // Managed helper that invokes the editor-provided GUID callback on the engine
  // thread.
  ref class CallbackInvoker sealed {
  public:
    CallbackInvoker(System::Guid guid, System::Action<System::Guid>^ onCreated)
      : guid_(guid), onCreated_(onCreated) {
    }

    void OnCreated(Oxygen::Managed::Core::NodeHandle) {
      try {
        if (onCreated_ != nullptr)
          onCreated_->Invoke(guid_);
      }
      catch (...) {
        // swallow to avoid crashing engine thread
      }
    }

  private:
    System::Guid guid_;
    System::Action<System::Guid>^ onCreated_;
  };

  OxygenWorld::OxygenWorld(EngineContext^ context)
    : OxygenWorld(context, nullptr) {
  }

  OxygenWorld::OxygenWorld(EngineContext^ context,
    ICommandFactory^ commandFactory)
    : context_(context) {
    if (context == nullptr)
      throw gcnew System::ArgumentNullException("context");

    if (commandFactory == nullptr) {
      commandFactory_ = gcnew CommandFactory();
    }
    else {
      commandFactory_ = commandFactory;
    }
  }

  void OxygenWorld::CreateScene(String^ name) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;

    msclr::interop::marshal_context marshal;
    auto native_name = marshal.marshal_as<std::string>(name);
    try {
      // Synchronously wait for async creation to complete to preserve
      // behavior of callers using the old synchronous API.
      auto task = CreateSceneAsync(name);
      try { task->GetAwaiter().GetResult(); }
      catch (...) { /* swallow */ }
    }
    catch (...) {
      // swallow
    }
  }

  void OxygenWorld::DestroyScene() {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;
    try {
      editor_module->get().DestroyScene();
    }
    catch (...) {
      // swallow to avoid crashing managed caller when engine thread isn't available
    }
  }

  System::Threading::Tasks::Task<bool>^ OxygenWorld::CreateSceneAsync(String^ name) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx)
      return CreateFailedBoolTask("CreateSceneAsync failed: native context is null");

    if (!native_ctx->engine)
      return CreateFailedBoolTask("CreateSceneAsync failed: native engine is null");

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return CreateFailedBoolTask("CreateSceneAsync failed: EditorModule is not available");

    msclr::interop::marshal_context marshal;
    auto native_name = marshal.marshal_as<std::string>(name);

    auto tcs = gcnew TaskCompletionSource<bool>();
    // Allocate a gcroot on the heap so the native callback can own it.
    auto* tcs_ptr = new msclr::gcroot<TaskCompletionSource<bool>^>(tcs);

    try {
      auto cb = MakeTcsCallback(tcs_ptr);
      editor_module->get().CreateScene(native_name, std::move(cb));
    }
    catch (const std::exception& e) {
      // Ensure we don't leak the allocated pointer on error
      try { delete tcs_ptr; }
      catch (...) {}
      tcs->SetException(gcnew System::InvalidOperationException(
        gcnew System::String(e.what())));
    }
    catch (...) {
      // Ensure we don't leak the allocated pointer on error
      try { delete tcs_ptr; }
      catch (...) {}
      tcs->SetException(gcnew System::InvalidOperationException(
        "CreateSceneAsync failed: unknown native exception"));
    }

    return tcs->Task;
  }

  void OxygenWorld::CreateSceneNode(String^ name, System::Guid nodeId,
    Nullable<System::Guid> parentGuid,
    Action<System::Guid>^ onCreated) {
    CreateSceneNode(name, nodeId, parentGuid, onCreated, false);
  }

  void OxygenWorld::CreateSceneNode(String^ name, System::Guid nodeId,
    Nullable<System::Guid> parentGuid,
    Action<System::Guid>^ onCreated,
    bool initializeWorldAsRoot) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;

    msclr::interop::marshal_context marshal;
    auto native_name = marshal.marshal_as<std::string>(name);

    // Resolve parent if provided
    oxygen::scene::NodeHandle native_parent;
    if (parentGuid.HasValue) {
      auto guid = parentGuid.Value;
      auto bytes = guid.ToByteArray();
      std::array<uint8_t, 16> key{};
      for (int i = 0; i < 16; ++i)
        key[i] = bytes[i];
      auto opt = NodeRegistry::Lookup(key);
      if (opt.has_value())
        native_parent = opt.value();
    }

    // Build registration key from caller-supplied nodeId (editor authoritative)
    auto idBytes = nodeId.ToByteArray();
    std::array<uint8_t, 16> reg_key{};
    for (int i = 0; i < 16; ++i)
      reg_key[i] = idBytes[i];

    // Create managed invoker that will be called on engine thread after
    // registration
    auto invoker = gcnew CallbackInvoker(nodeId, onCreated);
    auto managedCallback = gcnew System::Action<Oxygen::Managed::Core::NodeHandle>(
      invoker, &CallbackInvoker::OnCreated);

    // Enqueue command that will create native node, register native handle under
    // reg_key, then invoke managed callback
    auto cmd =
      std::unique_ptr<CreateSceneNodeCommand>(commandFactory_->CreateSceneNode(
        native_name, native_parent, managedCallback, reg_key,
        initializeWorldAsRoot));
    editor_module->get().Enqueue(std::move(cmd));
  }

  void OxygenWorld::RemoveSceneNode(System::Guid nodeId) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i)
      key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value())
      return;

    const auto& handle = opt.value();
    auto cmd = std::unique_ptr<RemoveSceneNodeCommand>(
      commandFactory_->CreateRemoveSceneNode(handle));
    editor_module->get().Enqueue(std::move(cmd));
  }

  void OxygenWorld::RemoveSceneNodes(array<System::Guid>^ nodes) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;

    std::vector<oxygen::scene::NodeHandle> native_nodes;
    if (nodes != nullptr) {
      native_nodes.reserve(nodes->Length);
      for (int i = 0; i < nodes->Length; ++i) {
        auto b = nodes[i].ToByteArray();
        std::array<uint8_t, 16> key{};
        for (int j = 0; j < 16; ++j)
          key[j] = b[j];
        auto opt = NodeRegistry::Lookup(key);
        if (opt.has_value()) {
          native_nodes.emplace_back(opt.value());
          // Best-effort unregister
          NodeRegistry::Unregister(key);
        }
      }
    }

    if (native_nodes.empty())
      return;

    auto cmd = std::unique_ptr<RemoveSceneNodesCommand>(
      commandFactory_->CreateRemoveSceneNodes(std::move(native_nodes)));
    editor_module->get().Enqueue(std::move(cmd));
  }

  void OxygenWorld::RenameSceneNode(System::Guid nodeId, String^ newName) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i)
      key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value())
      return;

    const auto& handle = opt.value();
    auto native_name = msclr::interop::marshal_as<std::string>(newName);
    auto cmd = std::unique_ptr<RenameSceneNodeCommand>(
      commandFactory_->CreateRenameSceneNode(handle, native_name));
    editor_module->get().Enqueue(std::move(cmd));
  }

  void OxygenWorld::SetLocalTransform(System::Guid nodeId,
    System::Numerics::Vector3 position,
    System::Numerics::Quaternion rotation,
    System::Numerics::Vector3 scale) {

    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i)
      key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value())
      return;

    const auto& nodeHandle = opt.value();

    glm::vec3 glm_pos(position.X, position.Y, position.Z);
    glm::quat glm_rot(rotation.W, rotation.X, rotation.Y, rotation.Z);
    glm::vec3 glm_scale(scale.X, scale.Y, scale.Z);

    auto cmd = std::unique_ptr<SetLocalTransformCommand>(
      commandFactory_->CreateSetLocalTransform(nodeHandle, glm_pos, glm_rot,
        glm_scale));
    editor_module->get().Enqueue(std::move(cmd));
  }

  namespace {

    // The native entry of a managed one; refuses an entry that is not a schema
    // field of its value type.
    auto ToNativeProperty(PropertyValueEntry entry)
      -> oxygen::interop::module::PropertyEntry
    {
      using oxygen::interop::module::FieldValue;
      FieldValue value;
      switch (entry.Type) {
      case FieldType::Bool:
        value = entry.BoolValue;
        break;
      case FieldType::Int32:
        value = static_cast<std::int32_t>(entry.Int32Value);
        break;
      case FieldType::Float:
        value = entry.FloatValue;
        break;
      default:
        throw gcnew System::ArgumentException(System::String::Format(
          "SetProperties failed because field {0} of component {1} has the unknown value type {2}.",
          entry.FieldId, entry.ComponentId, static_cast<System::Byte>(entry.Type)));
      }
      oxygen::interop::module::PropertyEntry native {
        static_cast<oxygen::interop::module::ComponentId>(entry.ComponentId),
        entry.FieldId, value };
      if (!oxygen::interop::module::IsWellTyped(native)) {
        throw gcnew System::ArgumentException(System::String::Format(
          "SetProperties failed because field {0} of component {1} is not a schema field of value type {2}.",
          entry.FieldId, entry.ComponentId, entry.Type));
      }
      return native;
    }

  } // namespace

  void OxygenWorld::SetProperties(System::Guid nodeId,
    array<PropertyValueEntry>^ entries) {
    if (entries == nullptr || entries->Length == 0) {
      return;
    }

    // Property pipeline §5.3: ensure the per-component dispatch
    // registry is populated before the first SetProperties command
    // runs. Bootstrap is idempotent (std::call_once).
    oxygen::interop::module::PropertyApplierRegistry::Bootstrap();

    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      throw gcnew System::InvalidOperationException(
        "SetProperties failed because the native engine context is not available.");
    }

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) {
      throw gcnew System::InvalidOperationException(
        "SetProperties failed because EditorModule is not available.");
    }

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i)
      key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value()) {
      throw gcnew System::ArgumentException(
        "SetProperties failed because the target scene node is not registered.");
    }

    const auto& nodeHandle = opt.value();

    std::vector<oxygen::interop::module::PropertyEntry> native_entries;
    native_entries.reserve(entries->Length);
    for (int i = 0; i < entries->Length; ++i) {
      native_entries.push_back(ToNativeProperty(entries[i]));
    }

    auto cmd = std::unique_ptr<SetPropertiesCommand>(
      commandFactory_->CreateSetProperties(nodeHandle, std::move(native_entries)));
    editor_module->get().Enqueue(std::move(cmd));
  }

  void OxygenWorld::SetGeometry(System::Guid nodeId, String^ assetUri,
    Action<System::UInt64, String^>^ onFailure) {
    SetGeometry(nodeId, assetUri, onFailure, nullptr);
  }

  void OxygenWorld::SetGeometry(System::Guid nodeId, String^ assetUri,
    Action<System::UInt64, String^>^ onFailure,
    Action<System::UInt64>^ onSuccess) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      throw gcnew InvalidOperationException("Runtime asset command has no engine context.");
    }

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) {
      throw gcnew InvalidOperationException("Runtime asset command has no editor module.");
    }

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i)
      key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value()) {
      throw gcnew InvalidOperationException("Runtime asset target is no longer registered.");
    }

    const auto& handle = opt.value();
    auto native_asset_uri = msclr::interop::marshal_as<std::string>(assetUri);
    auto cmd = std::unique_ptr<SetGeometryCommand>(
      commandFactory_->CreateSetGeometry(handle, native_asset_uri));
    cmd->SetFailureCallback(MakeAssetFailureCallback(onFailure));
    cmd->SetSuccessCallback(MakeAssetSuccessCallback(onSuccess));
    editor_module->get().Enqueue(std::move(cmd));
  }

  namespace {
    //! Copies a managed threshold list into a record's fixed boundary array.
    auto CopyBoundaries(array<float>^ source,
      float (&destination)[oxygen::data::pak::geometry::kMaxLodBoundaryCount])
      -> std::size_t {
      if (source == nullptr) {
        return 0U;
      }
      if (source->Length
        > static_cast<int>(oxygen::data::pak::geometry::kMaxLodBoundaryCount)) {
        throw gcnew ArgumentException("A LOD policy has at most 7 boundaries.", "entries");
      }
      for (int index = 0; index < source->Length; ++index) {
        destination[index] = source[index];
      }
      return static_cast<std::size_t>(source->Length);
    }

    //! The native record of a managed LOD policy, rejected unless the data
    //! rules hold: its type, counts, finite values and threshold order.
    auto ToNativeLodPolicy(LodPolicyManaged policy)
      -> oxygen::data::pak::geometry::LodPolicyRecord {
      using oxygen::data::pak::geometry::LodPolicyType;
      auto record = oxygen::data::pak::geometry::LodPolicyRecord {};
      record.type = static_cast<LodPolicyType>(policy.Type);
      record.lod_index = policy.LodIndex;
      record.hysteresis_ratio = policy.HysteresisRatio;
      const auto thresholds = CopyBoundaries(policy.Thresholds, record.thresholds);
      const auto enter = CopyBoundaries(policy.EnterFinerSse, record.enter_finer_sse);
      const auto exit = CopyBoundaries(policy.ExitCoarserSse, record.exit_coarser_sse);
      if (record.type == LodPolicyType::kScreenSpaceError && enter != exit) {
        throw gcnew ArgumentException(
          "A screen-space error policy needs as many exit as enter thresholds.", "entries");
      }
      record.boundary_count = static_cast<std::uint8_t>(
        record.type == LodPolicyType::kDistance ? thresholds : enter);
      if (!oxygen::data::IsValidLodPolicy(record)) {
        throw gcnew ArgumentException("The LOD policy is invalid.", "entries");
      }
      return record;
    }

    //! Validates each managed override against the schema property its id
    //! names and the addressed part kind, and reads its value by that
    //! property's value type.
    auto ToNativeOverrides(array<OverrideEntryManaged>^ entries,
      const oxygen::data::schema::PartKind part_kind)
      -> std::vector<OverrideEntry> {
      using oxygen::data::schema::ValueType;
      if (entries == nullptr) {
        throw gcnew ArgumentNullException("entries");
      }
      auto native = std::vector<OverrideEntry> {};
      native.reserve(static_cast<std::size_t>(entries->Length));
      for each (OverrideEntryManaged entry in entries) {
        const auto id = static_cast<oxygen::data::schema::OverrideId>(entry.OverrideId);
        const auto* property = oxygen::data::schema::FindOverrideProperty(id);
        if (property == nullptr || property->part_kind != part_kind) {
          throw gcnew ArgumentException(
            "An override names a property the addressed part does not have.", "entries");
        }
        if (std::ranges::find(native, id, &OverrideEntry::id) != native.end()) {
          throw gcnew ArgumentException(
            "A part holds at most one override per property.", "entries");
        }
        const bool flag_unused = !entry.Flag;
        const bool asset_unused = entry.AssetPath == nullptr;
        const bool lod_unused = entry.LodPolicy.Thresholds == nullptr
          && entry.LodPolicy.EnterFinerSse == nullptr
          && entry.LodPolicy.ExitCoarserSse == nullptr && entry.LodPolicy.Type == 0
          && entry.LodPolicy.LodIndex == 0 && entry.LodPolicy.HysteresisRatio == 0.0F;
        switch (property->type) {
        case ValueType::kBool:
          if (!asset_unused || !lod_unused) {
            throw gcnew ArgumentException("A bool override holds only its flag.", "entries");
          }
          native.push_back({ .id = id, .value = entry.Flag });
          break;
        case ValueType::kAsset:
          // Materials are the only asset overrides, and the only assets the
          // part loads.
          if (property->asset_type != oxygen::data::AssetType::kMaterial) {
            throw gcnew NotSupportedException("Only material asset overrides load at runtime.");
          }
          if (String::IsNullOrWhiteSpace(entry.AssetPath) || !flag_unused || !lod_unused) {
            throw gcnew ArgumentException(
              "An asset override holds only a nonempty asset path.", "entries");
          }
          native.push_back({ .id = id,
            .value = OverrideAsset {
              .virtual_path = msclr::interop::marshal_as<std::string>(entry.AssetPath) } });
          break;
        case ValueType::kLodPolicy:
          if (!flag_unused || !asset_unused) {
            throw gcnew ArgumentException(
              "A LOD policy override holds only its policy.", "entries");
          }
          native.push_back({ .id = id, .value = ToNativeLodPolicy(entry.LodPolicy) });
          break;
        case ValueType::kInt:
        case ValueType::kFloat:
        case ValueType::kVec3:
        case ValueType::kEnum:
        case ValueType::kString:
          // No override property has these types yet; each needs its reader
          // here when the schema declares one.
          throw gcnew NotSupportedException("The override value type is not supported.");
        }
      }
      return native;
    }
  } // namespace

  void OxygenWorld::SetOverrides(System::Guid nodeId, String^ geometryUri,
    System::Guid surfaceId, String^ layoutRevision,
    array<OverrideEntryManaged>^ entries, System::Byte intent,
    Action<System::UInt64, String^>^ onFailure,
    Action<System::UInt64>^ onSuccess) {
    const auto native_intent = static_cast<OverrideIntent>(intent);
    if (native_intent != OverrideIntent::kObservedEdit
      && native_intent != OverrideIntent::kRetainedOverride) {
      throw gcnew ArgumentOutOfRangeException("intent");
    }
    if (String::IsNullOrWhiteSpace(geometryUri)) {
      throw gcnew ArgumentException("Overrides require geometry identity.", "geometryUri");
    }
    OverrideTarget target {
      .geometry_uri = msclr::interop::marshal_as<std::string>(geometryUri),
      .surface_id = {},
      .layout_revision = {},
    };
    const auto is_surface = surfaceId != System::Guid::Empty;
    if (is_surface) {
      const auto native_surface = oxygen::data::SurfaceId::FromString(
        msclr::interop::marshal_as<std::string>(surfaceId.ToString("D")));
      if (!native_surface || native_surface.value().IsNil()) {
        throw gcnew ArgumentException("Surface identity is invalid.", "surfaceId");
      }
      target.surface_id = native_surface.value();
      constexpr auto kHexCharactersPerByte = 2U;
      if (layoutRevision == nullptr
        || layoutRevision->Length
          != static_cast<int>(target.layout_revision.size() * kHexCharactersPerByte)
        || !String::Equals(layoutRevision, layoutRevision->ToLowerInvariant(),
          StringComparison::Ordinal)) {
        throw gcnew ArgumentException(
          "Layout revision requires canonical SHA-256 text.", "layoutRevision");
      }
      const auto revision_bytes = Convert::FromHexString(layoutRevision);
      for (std::size_t index = 0; index < target.layout_revision.size(); ++index) {
        target.layout_revision.at(index) = revision_bytes[static_cast<int>(index)];
      }
      if (oxygen::base::IsAllZero(target.layout_revision)) {
        throw gcnew ArgumentException("Layout revision must not be empty.", "layoutRevision");
      }
    } else if (layoutRevision != nullptr) {
      throw gcnew ArgumentException(
        "The component's own overrides carry no layout revision.", "layoutRevision");
    }
    auto native_entries = ToNativeOverrides(entries,
      is_surface ? oxygen::data::schema::PartKind::kSurface
                 : oxygen::data::schema::PartKind::kNone);

    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      throw gcnew InvalidOperationException("Runtime asset command has no engine context.");
    }
    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) {
      throw gcnew InvalidOperationException("Runtime asset command has no editor module.");
    }
    const auto node_bytes = nodeId.ToByteArray();
    UuidKey key {};
    for (std::size_t index = 0; index < key.size(); ++index) {
      key.at(index) = node_bytes[static_cast<int>(index)];
    }
    const auto handle = NodeRegistry::Lookup(key);
    if (!handle) {
      throw gcnew InvalidOperationException("Runtime asset target is no longer registered.");
    }
    auto command = std::unique_ptr<SetOverridesCommand>(
      commandFactory_->CreateSetOverrides(handle.value(), std::move(target),
        std::move(native_entries), native_intent));
    command->SetFailureCallback(MakeAssetFailureCallback(onFailure));
    command->SetSuccessCallback(MakeAssetSuccessCallback(onSuccess));
    editor_module->get().Enqueue(std::move(command));
  }

  void OxygenWorld::SetEnvironment(bool atmosphereEnabled, bool sunDiskEnabled,
    float planetRadiusMeters, float atmosphereHeightMeters,
    System::Numerics::Vector3 groundAlbedoRgb, float rayleighScaleHeightMeters,
    float mieScaleHeightMeters, float mieAnisotropy,
    System::Numerics::Vector3 skyLuminanceFactorRgb,
    float aerialPerspectiveDistanceScale, float aerialScatteringStrength,
    float aerialPerspectiveStartDepthMeters, float heightFogContribution,
    int exposureMode, bool exposureEnabled, float exposureKey,
    float manualExposureEv, float exposureCompensation, int toneMapping,
    int autoExposureMeteringMode, float autoExposureMinEv,
    float autoExposureMaxEv, float autoExposureSpeedUp,
    float autoExposureSpeedDown, float autoExposureLowPercentile,
    float autoExposureHighPercentile, float autoExposureMinLogLuminance,
    float autoExposureLogLuminanceRange, float autoExposureTargetLuminance,
    float autoExposureSpotMeterRadius, float autoExposureBlackInfluence,
    float autoExposureTransitionDistanceEv,
    cli::array<ExposureCompensationKeyManaged> ^ autoExposureCompensationCurve,
    String ^ exposureMaskPath, float bloomIntensity,
    float bloomThreshold, float saturation, float contrast,
    float vignetteIntensity, float displayGamma, FogEnvironmentManaged fog,
    SkySphereEnvironmentManaged skySphere, SkyLightEnvironmentManaged skyLight,
    BackgroundEnvironmentManaged background,
    Action<System::UInt64, String ^> ^ onFailure,
    Action<System::UInt64> ^ onSuccess)
  {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      throw gcnew System::InvalidOperationException(
        "SetEnvironment failed because the native engine context is not available.");
    }

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) {
      throw gcnew System::InvalidOperationException(
        "SetEnvironment failed because EditorModule is not available.");
    }

    SkyAtmosphereParams atmosphere {};
    atmosphere.enabled = atmosphereEnabled;
    atmosphere.sun_disk_enabled = sunDiskEnabled;
    atmosphere.planet_radius_m = planetRadiusMeters;
    atmosphere.atmosphere_height_m = atmosphereHeightMeters;
    atmosphere.ground_albedo_rgb
      = oxygen::Vec3 { groundAlbedoRgb.X, groundAlbedoRgb.Y, groundAlbedoRgb.Z };
    atmosphere.rayleigh_scale_height_m = rayleighScaleHeightMeters;
    atmosphere.mie_scale_height_m = mieScaleHeightMeters;
    atmosphere.mie_anisotropy = mieAnisotropy;
    atmosphere.sky_luminance_factor_rgb = oxygen::Vec3 {
      skyLuminanceFactorRgb.X,
      skyLuminanceFactorRgb.Y,
      skyLuminanceFactorRgb.Z,
    };
    atmosphere.aerial_perspective_distance_scale
      = aerialPerspectiveDistanceScale;
    atmosphere.aerial_scattering_strength = aerialScatteringStrength;
    atmosphere.aerial_perspective_start_depth_m
      = aerialPerspectiveStartDepthMeters;
    atmosphere.height_fog_contribution = heightFogContribution;

    PostProcessParams post_process {};
    post_process.tone_mapper = toneMapping;
    post_process.exposure_mode = exposureMode;
    post_process.exposure_enabled = exposureEnabled;
    post_process.exposure_key = exposureKey;
    post_process.manual_exposure_ev = manualExposureEv;
    post_process.exposure_compensation_ev = exposureCompensation;
    post_process.auto_exposure_metering_mode = autoExposureMeteringMode;
    post_process.auto_exposure_min_ev = autoExposureMinEv;
    post_process.auto_exposure_max_ev = autoExposureMaxEv;
    post_process.auto_exposure_speed_up = autoExposureSpeedUp;
    post_process.auto_exposure_speed_down = autoExposureSpeedDown;
    post_process.auto_exposure_low_percentile = autoExposureLowPercentile;
    post_process.auto_exposure_high_percentile = autoExposureHighPercentile;
    post_process.auto_exposure_min_log_luminance
      = autoExposureMinLogLuminance;
    post_process.auto_exposure_log_luminance_range
      = autoExposureLogLuminanceRange;
    post_process.auto_exposure_target_luminance = autoExposureTargetLuminance;
    post_process.auto_exposure_spot_meter_radius = autoExposureSpotMeterRadius;
    post_process.auto_exposure_black_influence = autoExposureBlackInfluence;
    post_process.auto_exposure_transition_distance_ev = autoExposureTransitionDistanceEv;
    if (autoExposureCompensationCurve == nullptr
      || autoExposureCompensationCurve->Length > oxygen::engine::kMaxExposureCompensationCurveKeys) {
      throw gcnew System::ArgumentException("Exposure compensation curve must contain at most 64 keys.", "autoExposureCompensationCurve");
    }
    for each (const auto key in autoExposureCompensationCurve) {
      post_process.auto_exposure_compensation_curve.push_back({
        .metered_ev = key.MeteredEv, .compensation_ev = key.CompensationEv,
      });
    }
    if (exposureMaskPath != nullptr) {
      if (String::IsNullOrWhiteSpace(exposureMaskPath)) {
        throw gcnew ArgumentException("An exposure mask requires a texture virtual path.", "exposureMaskPath");
      }
      post_process.auto_exposure_metering_mask = oxygen::interop::ToUtf8(exposureMaskPath);
    }
    post_process.bloom_intensity = bloomIntensity;
    post_process.bloom_threshold = bloomThreshold;
    post_process.saturation = saturation;
    post_process.contrast = contrast;
    post_process.vignette_intensity = vignetteIntensity;
    post_process.display_gamma = displayGamma;

    auto cmd = std::unique_ptr<SetEnvironmentCommand>(
      commandFactory_->CreateSetEnvironment(
        atmosphere, std::move(post_process), ToNativeFog(fog),
        ToNativeSky(skySphere, skyLight, background)));
    cmd->SetFailureCallback(MakeAssetFailureCallback(onFailure));
    cmd->SetSuccessCallback(MakeAssetSuccessCallback(onSuccess));
    editor_module->get().Enqueue(std::move(cmd));
  }

  void OxygenWorld::DetachGeometry(System::Guid nodeId) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i)
      key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value())
      return;

    const auto& handle = opt.value();
    auto cmd = std::unique_ptr<DetachGeometryCommand>(
      commandFactory_->CreateDetachGeometry(handle));
    editor_module->get().Enqueue(std::move(cmd));
  }

  namespace {

    //! The camera aspect mode `aspectMode` names; throws when it names none.
    auto ToAspectMode(System::Byte aspectMode) -> oxygen::CameraAspectMode
    {
      const auto mode = interop::ToCameraAspectMode(aspectMode);
      if (!mode) {
        throw gcnew System::ArgumentOutOfRangeException(
          "aspectMode", "The camera aspect mode is not a schema member.");
      }
      return *mode;
    }

  } // namespace

  void OxygenWorld::AttachPerspectiveCamera(System::Guid nodeId,
    float fieldOfViewYRadians, float aspectRatio, float nearPlane,
    float farPlane, System::Byte aspectMode) {
    const auto target
      = RequireNodeForCommand(context_, nodeId, "AttachPerspectiveCamera");
    std::unique_ptr<AttachPerspectiveCameraCommand> cmd;
    try {
      cmd.reset(commandFactory_->CreateAttachPerspectiveCamera(target.node,
        fieldOfViewYRadians, aspectRatio, nearPlane, farPlane,
        ToAspectMode(aspectMode)));
    } catch (const std::invalid_argument& error) {
      throw gcnew System::ArgumentException(
        gcnew System::String(error.what()));
    }
    target.module.Enqueue(std::move(cmd));
  }

  void OxygenWorld::AttachOrthographicCamera(System::Guid nodeId,
    float orthographicSize, float aspectRatio, float nearPlane,
    float farPlane, System::Byte aspectMode) {
    const auto target
      = RequireNodeForCommand(context_, nodeId, "AttachOrthographicCamera");
    std::unique_ptr<AttachOrthographicCameraCommand> cmd;
    try {
      cmd.reset(commandFactory_->CreateAttachOrthographicCamera(target.node,
        orthographicSize, aspectRatio, nearPlane, farPlane,
        ToAspectMode(aspectMode)));
    } catch (const std::invalid_argument& error) {
      throw gcnew System::ArgumentException(
        gcnew System::String(error.what()));
    }
    target.module.Enqueue(std::move(cmd));
  }

  void OxygenWorld::DetachCamera(System::Guid nodeId) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i)
      key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value())
      return;

    const auto& handle = opt.value();
    auto cmd = std::unique_ptr<DetachCameraCommand>(
      commandFactory_->CreateDetachCamera(handle));
    editor_module->get().Enqueue(std::move(cmd));
  }

  void OxygenWorld::AttachDirectionalLight(System::Guid nodeId,
    float intensityLux, float angularSizeRadians, System::Numerics::Vector3 color,
    bool affectsWorld, int mobility, bool castsShadows, float shadowBias,
    float shadowNormalBias, bool contactShadows, int shadowResolutionHint,
    float exposureCompensation, int atmosphereLightSlot, bool usePerPixelAtmosphereTransmittance,
    System::Numerics::Vector3 atmosphereDiskLuminanceScaleRgb,
    int cascadeCount, int splitMode, float maxShadowDistance,
    System::Numerics::Vector4 cascadeDistances, float distributionExponent,
    float transitionFraction, float distanceFadeoutFraction)
  {
    const interop::DirectionalLightValues values {
      .common = ToLightCommonValues(color, affectsWorld, mobility, castsShadows,
        shadowBias, shadowNormalBias, contactShadows, shadowResolutionHint,
        exposureCompensation),
      .intensity_lux = intensityLux,
      .angular_size_radians = angularSizeRadians,
      .atmosphere_light_slot = atmosphereLightSlot,
      .use_per_pixel_atmosphere_transmittance
      = usePerPixelAtmosphereTransmittance,
      .disk_luminance_scale = ToVec3(atmosphereDiskLuminanceScaleRgb),
      .cascade_count = cascadeCount,
      .split_mode = splitMode,
      .max_shadow_distance = maxShadowDistance,
      .cascade_distances = { cascadeDistances.X, cascadeDistances.Y,
        cascadeDistances.Z, cascadeDistances.W },
      .distribution_exponent = distributionExponent,
      .transition_fraction = transitionFraction,
      .distance_fadeout_fraction = distanceFadeoutFraction,
    };
    std::unique_ptr<oxygen::scene::DirectionalLight> light;
    try {
      light = interop::MakeDirectionalLight(values);
    } catch (const std::invalid_argument& error) {
      throw gcnew System::ArgumentException(gcnew System::String(error.what()));
    }
    EnqueueAttachLight(context_, nodeId, std::move(light));
  }

  void OxygenWorld::AttachPointLight(System::Guid nodeId, float luminousFluxLumens,
    float range, float sourceRadius, System::Numerics::Vector3 color,
    bool affectsWorld, bool castsShadows, float shadowBias, float shadowNormalBias,
    bool contactShadows, int shadowResolutionHint, float exposureCompensation)
  {
    const interop::PointLightValues values {
      .common = ToLightCommonValues(color, affectsWorld, 0, castsShadows,
        shadowBias, shadowNormalBias, contactShadows, shadowResolutionHint,
        exposureCompensation),
      .luminous_flux_lm = luminousFluxLumens,
      .range = range,
      .source_radius = sourceRadius,
    };
    std::unique_ptr<oxygen::scene::PointLight> light;
    try {
      light = interop::MakePointLight(values);
    } catch (const std::invalid_argument& error) {
      throw gcnew System::ArgumentException(gcnew System::String(error.what()));
    }
    EnqueueAttachLight(context_, nodeId, std::move(light));
  }

  void OxygenWorld::AttachSpotLight(System::Guid nodeId, float luminousFluxLumens,
    float range, float sourceRadius, float innerConeAngleRadians, float outerConeAngleRadians,
    System::Numerics::Vector3 color, bool affectsWorld, bool castsShadows,
    float shadowBias, float shadowNormalBias, bool contactShadows,
    int shadowResolutionHint, float exposureCompensation)
  {
    const interop::SpotLightValues values {
      .local = {
        .common = ToLightCommonValues(color, affectsWorld, 0, castsShadows,
          shadowBias, shadowNormalBias, contactShadows, shadowResolutionHint,
          exposureCompensation),
        .luminous_flux_lm = luminousFluxLumens,
        .range = range,
        .source_radius = sourceRadius,
      },
      .inner_cone_angle_radians = innerConeAngleRadians,
      .outer_cone_angle_radians = outerConeAngleRadians,
    };
    std::unique_ptr<oxygen::scene::SpotLight> light;
    try {
      light = interop::MakeSpotLight(values);
    } catch (const std::invalid_argument& error) {
      throw gcnew System::ArgumentException(gcnew System::String(error.what()));
    }
    EnqueueAttachLight(context_, nodeId, std::move(light));
  }

  void OxygenWorld::DetachLight(System::Guid nodeId) {
    EnqueueDetachLight(context_, nodeId);
  }

  void OxygenWorld::AttachLocalFogVolume(System::Guid nodeId, bool enabled,
    float radialFogExtinction, float heightFogExtinction,
    float heightFogFalloff, float heightFogOffset, float fogPhaseG,
    System::Numerics::Vector3 fogAlbedo, System::Numerics::Vector3 fogEmissive,
    int sortPriority)
  {
    oxygen::scene::environment::LocalFogVolume volume;
    try {
      volume = MakeLocalFogVolume({
        .enabled = enabled,
        .radial_fog_extinction = radialFogExtinction,
        .height_fog_extinction = heightFogExtinction,
        .height_fog_falloff = heightFogFalloff,
        .height_fog_offset = heightFogOffset,
        .fog_phase_g = fogPhaseG,
        .fog_albedo = ToVec3(fogAlbedo),
        .fog_emissive = ToVec3(fogEmissive),
        .sort_priority = sortPriority,
      });
    } catch (const std::invalid_argument& error) {
      throw gcnew System::ArgumentException(
        gcnew System::String(error.what()));
    }
    const auto target
      = RequireNodeForCommand(context_, nodeId, "AttachLocalFogVolume");
    target.module.Enqueue(
      std::make_unique<AttachLocalFogVolumeCommand>(target.node, volume));
  }

  void OxygenWorld::DetachLocalFogVolume(System::Guid nodeId) {
    if (const auto handle = LookupNodeForCommand(context_, nodeId)) {
      handle->module.Enqueue(
        std::make_unique<DetachLocalFogVolumeCommand>(handle->node));
    }
  }

  void OxygenWorld::ReparentSceneNode(System::Guid child,
    Nullable<System::Guid> parent,
    bool preserveWorldTransform) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;

    auto cb = child.ToByteArray();
    std::array<uint8_t, 16> childKey{};
    for (int i = 0; i < 16; ++i)
      childKey[i] = cb[i];
    auto childOpt = NodeRegistry::Lookup(childKey);
    if (!childOpt.has_value())
      return;
    const auto& childHandle = childOpt.value();

    oxygen::scene::NodeHandle parentHandle;
    if (parent.HasValue) {
      auto pb = parent.Value.ToByteArray();
      std::array<uint8_t, 16> pKey{};
      for (int i = 0; i < 16; ++i)
        pKey[i] = pb[i];
      auto popt = NodeRegistry::Lookup(pKey);
      if (!popt.has_value())
        return; // parent missing
      parentHandle = popt.value();
    }

    auto cmd = std::unique_ptr<ReparentSceneNodeCommand>(
      commandFactory_->CreateReparentSceneNode(childHandle, parentHandle,
        preserveWorldTransform));
    editor_module->get().Enqueue(std::move(cmd));
  }

  void OxygenWorld::ReparentSceneNodes(array<System::Guid>^ children,
    Nullable<System::Guid> parent,
    bool preserveWorldTransform) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;

    std::vector<oxygen::scene::NodeHandle> native_children;
    if (children != nullptr) {
      native_children.reserve(children->Length);
      for (int i = 0; i < children->Length; ++i) {
        auto cb = children[i].ToByteArray();
        std::array<uint8_t, 16> childKey{};
        for (int j = 0; j < 16; ++j)
          childKey[j] = cb[j];
        auto childOpt = NodeRegistry::Lookup(childKey);
        if (childOpt.has_value())
          native_children.emplace_back(childOpt.value());
      }
    }

    if (native_children.empty())
      return;

    oxygen::scene::NodeHandle parentHandle;
    if (parent.HasValue) {
      auto pb = parent.Value.ToByteArray();
      std::array<uint8_t, 16> pKey{};
      for (int i = 0; i < 16; ++i)
        pKey[i] = pb[i];
      auto popt = NodeRegistry::Lookup(pKey);
      if (!popt.has_value())
        return; // parent missing
      parentHandle = popt.value();
    }

    auto cmd = std::unique_ptr<ReparentSceneNodesCommand>(
      commandFactory_->CreateReparentSceneNodes(
        std::move(native_children), parentHandle, preserveWorldTransform));
    editor_module->get().Enqueue(std::move(cmd));
  }

  void OxygenWorld::UpdateTransformsForNodes(array<System::Guid>^ nodes) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      return;

    std::vector<oxygen::scene::NodeHandle> native_nodes;
    if (nodes != nullptr) {
      native_nodes.reserve(nodes->Length);
      for (int i = 0; i < nodes->Length; ++i) {
        auto b = nodes[i].ToByteArray();
        std::array<uint8_t, 16> key{};
        for (int j = 0; j < 16; ++j)
          key[j] = b[j];
        auto opt = NodeRegistry::Lookup(key);
        if (opt.has_value())
          native_nodes.emplace_back(opt.value());
      }
    }

    if (native_nodes.empty())
      return;
    auto cmd = std::unique_ptr<UpdateTransformsForNodesCommand>(
      commandFactory_->CreateUpdateTransformsForNodes(std::move(native_nodes)));
    editor_module->get().Enqueue(std::move(cmd));
  }

  void OxygenWorld::SelectNode(System::Guid nodeId) {
    // TODO: implement selection state handling (editor-side)
  }

  void OxygenWorld::DeselectNode(System::Guid nodeId) {
    // TODO: implement selection state handling (editor-side)
  }

  Task
    ^ OxygenWorld::ReplaceCookedRootsAsync(
      array<CookedRootManaged> ^ roots)
  {
    if (roots == nullptr) {
      throw gcnew System::ArgumentNullException("roots");
    }
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      throw gcnew System::InvalidOperationException("The native runtime is unavailable.");
    }
    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) {
      throw gcnew System::InvalidOperationException("The editor module is unavailable.");
    }
    std::vector<oxygen::content::ContentMount> native_roots;
    native_roots.reserve(static_cast<std::size_t>(roots->Length));
    for each (CookedRootManaged root in roots) {
      if (String::IsNullOrWhiteSpace(root.Path)) {
        throw gcnew System::ArgumentException(
          "A cooked root needs an absolute path.", "roots");
      }
      native_roots.push_back({
        .kind = root.Kind == CookedRootKindManaged::PakFile
          ? oxygen::content::ContentMount::Kind::kPakFile
          : oxygen::content::ContentMount::Kind::kLooseCookedRoot,
        .path = msclr::interop::marshal_as<std::wstring>(root.Path),
      });
    }
    auto completion = gcnew TaskCompletionSource<bool>(TaskCreationOptions::RunContinuationsAsynchronously);
    editor_module->get().ReplaceCookedRoots(std::move(native_roots),
      MakeCookedRootsCallback(completion));
    return completion->Task;
  }

  Task^ OxygenWorld::SetCookedContentPausedAsync(bool paused) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine)
      throw gcnew System::InvalidOperationException("The native runtime is unavailable.");
    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module)
      throw gcnew System::InvalidOperationException("The editor module is unavailable.");
    auto completion = gcnew TaskCompletionSource<bool>(TaskCreationOptions::RunContinuationsAsynchronously);
    editor_module->get().SetCookedContentPaused(paused,
      MakeCookedRootsCallback(completion));
    return completion->Task;
  }

} // namespace Oxygen::Interop::World
