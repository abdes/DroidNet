//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <stdexcept>
#include <utility>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  //! Owns a complete detached light until the scene mutation phase accepts it.
  class AttachLightCommand final : public EditorCommand {
  public:
    AttachLightCommand(scene::NodeHandle node, std::unique_ptr<Component> light)
      : EditorCommand(core::PhaseId::kSceneMutation)
      , node_(node)
      , light_(std::move(light))
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) return;
      auto node = context.Scene->GetNode(node_);
      if (!node || !node->IsAlive()) return;
      scene::LightValidationError error;
      if (!node->ReplaceLight(std::move(light_), &error)) {
        throw std::invalid_argument(error.field + ": " + error.message);
      }
      context.Scene->Update(false);
      context.Scene->SyncObservers();
    }

  private:
    scene::NodeHandle node_;
    std::unique_ptr<Component> light_;
  };

  class DetachLightCommand final : public EditorCommand {
  public:
    explicit DetachLightCommand(scene::NodeHandle node)
      : EditorCommand(core::PhaseId::kSceneMutation)
      , node_(node)
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) return;
      auto node = context.Scene->GetNode(node_);
      if (!node || !node->IsAlive()) return;
      static_cast<void>(node->DetachLight());
      context.Scene->Update(false);
      context.Scene->SyncObservers();
    }

  private:
    scene::NodeHandle node_;
  };
} // namespace oxygen::interop::module
