//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define WIN32_LEAN_AND_MEAN

#include <msclr/marshal_cppstd.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>

#include "OxygenWorld.h"
#include "Unmanaged/EditorModule.h"

namespace Oxygen::Interop::World {

  using namespace Oxygen::Editor::EngineInterface;

    OxygenWorld::OxygenWorld(EngineContext^ context) : context_(context) {
        if (context == nullptr) {
            throw gcnew System::ArgumentNullException("context");
        }
    }

    void OxygenWorld::CreateScene(String^ name) {
        msclr::interop::marshal_context marshal;
        auto native_name_for_log = marshal.marshal_as<std::string>(name);
        auto native_ctx = context_->NativePtr();
        if (!native_ctx || !native_ctx->engine) {
            return;
        }

        auto editor_module = native_ctx->engine->GetModule<EditorModule>();
        if (editor_module) {
            msclr::interop::marshal_context marshal;
            auto native_name = marshal.marshal_as<std::string>(name);
            editor_module->get().CreateScene(native_name);
        }
    }

    void OxygenWorld::CreateSceneNode(String^ name, String^ parentName) {
        msclr::interop::marshal_context marshal;
        auto native_name_for_log = marshal.marshal_as<std::string>(name);
        auto native_parent_for_log = parentName ? marshal.marshal_as<std::string>(parentName) : std::string();
        auto native_ctx = context_->NativePtr();
        if (!native_ctx || !native_ctx->engine) {
            return;
        }

        auto editor_module = native_ctx->engine->GetModule<EditorModule>();
        if (editor_module) {
            msclr::interop::marshal_context marshal;
            auto native_name = marshal.marshal_as<std::string>(name);
            auto native_parent = parentName ? marshal.marshal_as<std::string>(parentName) : "";
            editor_module->get().CreateSceneNode(native_name, native_parent);
        }
    }

    void OxygenWorld::RemoveSceneNode(String^ name) {
        auto native_ctx = context_->NativePtr();
        if (!native_ctx || !native_ctx->engine) {
            return;
        }

        auto editor_module = native_ctx->engine->GetModule<EditorModule>();
        if (editor_module) {
            msclr::interop::marshal_context marshal;
            auto native_name = marshal.marshal_as<std::string>(name);
            editor_module->get().RemoveSceneNode(native_name);
        }
    }

    void OxygenWorld::SetLocalTransform(String^ nodeName,
        System::Numerics::Vector3 position,
        System::Numerics::Quaternion rotation,
        System::Numerics::Vector3 scale) {
        auto native_ctx = context_->NativePtr();
        if (!native_ctx || !native_ctx->engine) {
            return;
        }

        auto editor_module = native_ctx->engine->GetModule<EditorModule>();
        if (editor_module) {
            msclr::interop::marshal_context marshal;
            auto native_name = marshal.marshal_as<std::string>(nodeName);

            // Convert System::Numerics to glm
            glm::vec3 glm_pos(position.X, position.Y, position.Z);
            glm::quat glm_rot(rotation.W, rotation.X, rotation.Y, rotation.Z);
            glm::vec3 glm_scale(scale.X, scale.Y, scale.Z);

            editor_module->get().SetLocalTransform(native_name, glm_pos, glm_rot, glm_scale);
        }
    }

    void OxygenWorld::CreateBasicMesh(String^ nodeName, String^ meshType) {
        msclr::interop::marshal_context marshal;
        auto native_node_for_log = marshal.marshal_as<std::string>(nodeName);
        auto native_mesh_for_log = marshal.marshal_as<std::string>(meshType);
        auto native_ctx = context_->NativePtr();
        if (!native_ctx || !native_ctx->engine) {
            return;
        }

        auto editor_module = native_ctx->engine->GetModule<EditorModule>();
        if (editor_module) {
            msclr::interop::marshal_context marshal;
            auto native_node_name = marshal.marshal_as<std::string>(nodeName);
            auto native_mesh_type = marshal.marshal_as<std::string>(meshType);
            editor_module->get().CreateBasicMesh(native_node_name, native_mesh_type);
        }
    }

} // namespace Oxygen::Interop::World
