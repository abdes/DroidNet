//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include <msclr/auto_gcroot.h>

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/NodeRegistry.h>

namespace oxygen::interop::module {

  class CreateSceneNodeCommand : public EditorCommand {
  public:
    using Callback = std::function<void(oxygen::scene::NodeHandle)>;

    // Overload for managed callbacks
    CreateSceneNodeCommand(std::string name, oxygen::scene::NodeHandle parent,
      System::Action<Oxygen::Editor::Core::NodeHandle>^
      managedCallback,
      bool initializeWorldAsRoot = false);

    // Overload for managed callbacks with registration key
    CreateSceneNodeCommand(std::string name, oxygen::scene::NodeHandle parent,
      System::Action<Oxygen::Editor::Core::NodeHandle>^
      managedCallback,
      const std::array<uint8_t, 16>& registerKey,
      bool initializeWorldAsRoot = false);

    void Execute(CommandContext& context) override;

  private:
    std::string name_;
    oxygen::scene::NodeHandle parent_;
    Callback callback_;
    msclr::auto_gcroot<System::Action<Oxygen::Editor::Core::NodeHandle>^>
      managed_callback_;
    bool has_managed_callback_;
    std::array<uint8_t, 16> register_key_{};
    bool has_register_key_;
    bool initialize_world_as_root_ = false;
  };

  // Overload for managed callbacks
  inline CreateSceneNodeCommand::CreateSceneNodeCommand(
    std::string name, oxygen::scene::NodeHandle parent,
    System::Action<Oxygen::Editor::Core::NodeHandle>^ managedCallback,
    bool initializeWorldAsRoot)
    : EditorCommand(oxygen::core::PhaseId::kSceneMutation),
    name_(std::move(name)), parent_(parent),
    managed_callback_(managedCallback), has_managed_callback_(true),
    has_register_key_(false),
    initialize_world_as_root_(initializeWorldAsRoot) {
  }

  // Overload for managed callbacks with registration key
  inline CreateSceneNodeCommand::CreateSceneNodeCommand(
    std::string name, oxygen::scene::NodeHandle parent,
    System::Action<Oxygen::Editor::Core::NodeHandle>^ managedCallback,
    const std::array<uint8_t, 16>& registerKey, bool initializeWorldAsRoot)
    : EditorCommand(oxygen::core::PhaseId::kSceneMutation),
    name_(std::move(name)), parent_(parent),
    managed_callback_(managedCallback), has_managed_callback_(true),
    register_key_(registerKey), has_register_key_(true),
    initialize_world_as_root_(initializeWorldAsRoot) {
  }

  inline void CreateSceneNodeCommand::Execute(CommandContext& context) {
    if (!context.Scene) {
      return;
    }

    oxygen::scene::SceneNode node;
    if (parent_.IsValid()) {
      auto parentNode = context.Scene->GetNode(parent_);
      if (parentNode && parentNode->IsAlive()) {
        auto result = context.Scene->CreateChildNode(*parentNode, name_);
        if (result) {
          node = *result;
        }
      }
    }
    else {
      node = context.Scene->CreateNode(name_);
    }

    const auto& handle = node.GetHandle();

    // If requested, initialize this node's world transform as root
    if (initialize_world_as_root_ && node.IsValid()) {
      try {
        auto impl_opt = context.Scene->GetNodeImpl(node);
        if (impl_opt.has_value()) {
          auto& impl = impl_opt->get();
          auto& tf =
            impl.GetComponent<oxygen::scene::detail::TransformComponent>();
          tf.UpdateWorldTransformAsRoot();
        }
      }
      catch (...) {
        // Swallow any errors - initialization is best-effort
      }
    }

    // If a registration key was provided, register native handle BEFORE
    // invoking managed callback.
    if (has_register_key_ && node.IsValid()) {
      NodeRegistry::Register(register_key_, handle);
    }

    if (has_managed_callback_ && managed_callback_.get()) {
      // Invoke managed callback - construct NodeHandle with the Value property
      Oxygen::Editor::Core::NodeHandle managedHandle(handle.Handle());
      managed_callback_->Invoke(managedHandle);
    }
    else if (callback_) {
      // Invoke native callback
      callback_(handle);
    }
  }

} // namespace oxygen::interop::module

#pragma managed(pop)
