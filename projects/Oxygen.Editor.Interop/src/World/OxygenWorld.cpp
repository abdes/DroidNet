//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define WIN32_LEAN_AND_MEAN

#include <msclr/marshal_cppstd.h>
#include <msclr/auto_gcroot.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Base/LoguruWrapper.h"
#include "World/OxygenWorld.h"
#include "EditorModule/EditorModule.h"
#include "EditorModule/NodeRegistry.h"
#include "Commands/SceneCommands.h"

using namespace oxygen::interop::module;
using namespace oxygen::interop::module::commands;

namespace Oxygen::Interop::World {

// Managed helper that invokes the editor-provided GUID callback on the engine thread.
ref class CallbackInvoker sealed {
public:
    CallbackInvoker(System::Guid guid, System::Action<System::Guid>^ onCreated)
        : guid_(guid), onCreated_(onCreated) {}

    void OnCreated(Oxygen::Editor::Core::NodeHandle) {
        try {
            if (onCreated_ != nullptr) onCreated_->Invoke(guid_);
        }
        catch (...) {
            // swallow to avoid crashing engine thread
        }
    }

private:
    System::Guid guid_;
    System::Action<System::Guid>^ onCreated_;
};

OxygenWorld::OxygenWorld(EngineContext^ context) : context_(context) {
    if (context == nullptr) throw gcnew System::ArgumentNullException("context");
}

void OxygenWorld::CreateScene(String^ name) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) return;

    msclr::interop::marshal_context marshal;
    auto native_name = marshal.marshal_as<std::string>(name);
    editor_module->get().CreateScene(native_name);
}

void OxygenWorld::CreateSceneNode(String^ name, System::Guid nodeId, Nullable<System::Guid> parentGuid, Action<System::Guid>^ onCreated) {
    CreateSceneNode(name, nodeId, parentGuid, onCreated, false);
}

void OxygenWorld::CreateSceneNode(String^ name, System::Guid nodeId, Nullable<System::Guid> parentGuid, Action<System::Guid>^ onCreated, bool initializeWorldAsRoot) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) return;

    msclr::interop::marshal_context marshal;
    auto native_name = marshal.marshal_as<std::string>(name);

    // Resolve parent if provided
    oxygen::scene::NodeHandle native_parent;
    if (parentGuid.HasValue) {
        auto guid = parentGuid.Value;
        auto bytes = guid.ToByteArray();
        std::array<uint8_t, 16> key{};
        for (int i = 0; i < 16; ++i) key[i] = bytes[i];
        auto opt = NodeRegistry::Lookup(key);
        if (opt.has_value()) native_parent = opt.value();
    }

    // Build registration key from caller-supplied nodeId (editor authoritative)
    auto idBytes = nodeId.ToByteArray();
    std::array<uint8_t, 16> reg_key{};
    for (int i = 0; i < 16; ++i) reg_key[i] = idBytes[i];

    // Create managed invoker that will be called on engine thread after registration
    auto invoker = gcnew CallbackInvoker(nodeId, onCreated);
    auto managedCallback = gcnew System::Action<Oxygen::Editor::Core::NodeHandle>(invoker, &CallbackInvoker::OnCreated);

    // Enqueue command that will create native node, register native handle under reg_key, then invoke managed callback
    auto cmd = std::make_unique<CreateSceneNodeCommand>(native_name, native_parent, managedCallback, reg_key, initializeWorldAsRoot);
    editor_module->get().Enqueue(std::move(cmd));
}

void OxygenWorld::RemoveSceneNode(System::Guid nodeId) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) return;

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i) key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value()) return;

    auto handle = opt.value();
    auto cmd = std::make_unique<RemoveSceneNodeCommand>(handle);
    editor_module->get().Enqueue(std::move(cmd));

    // Best-effort unregister
    NodeRegistry::Unregister(key);
}

void OxygenWorld::RenameSceneNode(System::Guid nodeId, String^ newName) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) return;

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i) key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value()) return;

    auto handle = opt.value();
    auto native_name = msclr::interop::marshal_as<std::string>(newName);
    auto cmd = std::make_unique<RenameSceneNodeCommand>(handle, native_name);
    editor_module->get().Enqueue(std::move(cmd));
}

void OxygenWorld::SetLocalTransform(System::Guid nodeId,
    System::Numerics::Vector3 position,
    System::Numerics::Quaternion rotation,
    System::Numerics::Vector3 scale) {

    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) return;

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i) key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value()) return;

    auto nodeHandle = opt.value();

    glm::vec3 glm_pos(position.X, position.Y, position.Z);
    glm::quat glm_rot(rotation.W, rotation.X, rotation.Y, rotation.Z);
    glm::vec3 glm_scale(scale.X, scale.Y, scale.Z);

    auto cmd = std::make_unique<SetLocalTransformCommand>(nodeHandle, glm_pos, glm_rot, glm_scale);
    editor_module->get().Enqueue(std::move(cmd));
}

void OxygenWorld::CreateBasicMesh(System::Guid nodeId, String^ meshType) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) return;

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i) key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value()) return;

    auto handle = opt.value();
    auto native_mesh_type = msclr::interop::marshal_as<std::string>(meshType);
    auto cmd = std::make_unique<CreateBasicMeshCommand>(handle, native_mesh_type);
    editor_module->get().Enqueue(std::move(cmd));
}

void OxygenWorld::SetVisibility(System::Guid nodeId, bool visible) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) return;

    auto b = nodeId.ToByteArray();
    std::array<uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i) key[i] = b[i];

    auto opt = NodeRegistry::Lookup(key);
    if (!opt.has_value()) return;

    auto handle = opt.value();
    auto cmd = std::make_unique<SetVisibilityCommand>(handle, visible);
    editor_module->get().Enqueue(std::move(cmd));
}

void OxygenWorld::ReparentSceneNode(System::Guid child, Nullable<System::Guid> parent, bool preserveWorldTransform) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) return;

    auto cb = child.ToByteArray();
    std::array<uint8_t, 16> childKey{};
    for (int i = 0; i < 16; ++i) childKey[i] = cb[i];
    auto childOpt = NodeRegistry::Lookup(childKey);
    if (!childOpt.has_value()) return;
    auto childHandle = childOpt.value();

    oxygen::scene::NodeHandle parentHandle;
    if (parent.HasValue) {
        auto pb = parent.Value.ToByteArray();
        std::array<uint8_t, 16> pKey{};
        for (int i = 0; i < 16; ++i) pKey[i] = pb[i];
        auto popt = NodeRegistry::Lookup(pKey);
        if (!popt.has_value()) return; // parent missing
        parentHandle = popt.value();
    }

    auto cmd = std::make_unique<ReparentSceneNodeCommand>(childHandle, parentHandle, preserveWorldTransform);
    editor_module->get().Enqueue(std::move(cmd));
}

void OxygenWorld::UpdateTransformsForNodes(array<System::Guid>^ nodes) {
    auto native_ctx = context_->NativePtr();
    if (!native_ctx || !native_ctx->engine) return;

    auto editor_module = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module) return;

    std::vector<oxygen::scene::NodeHandle> native_nodes;
    if (nodes != nullptr) {
        native_nodes.reserve(nodes->Length);
        for (int i = 0; i < nodes->Length; ++i) {
            auto b = nodes[i].ToByteArray();
            std::array<uint8_t, 16> key{};
            for (int j = 0; j < 16; ++j) key[j] = b[j];
            auto opt = NodeRegistry::Lookup(key);
            if (opt.has_value()) native_nodes.emplace_back(opt.value());
        }
    }

    if (native_nodes.empty()) return;
    auto cmd = std::make_unique<UpdateTransformsForNodesCommand>(std::move(native_nodes));
    editor_module->get().Enqueue(std::move(cmd));
}

void OxygenWorld::SelectNode(System::Guid nodeId) {
    // TODO: implement selection state handling (editor-side)
}

void OxygenWorld::DeselectNode(System::Guid nodeId) {
    // TODO: implement selection state handling (editor-side)
}

} // namespace Oxygen::Interop::World
