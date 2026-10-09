//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <EditorModule/EditorCommand.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

  //! Attaches or replaces the local fog volume on one node.
  class AttachLocalFogVolumeCommand final : public EditorCommand {
  public:
    AttachLocalFogVolumeCommand(
      scene::NodeHandle node, scene::environment::LocalFogVolume volume)
      : EditorCommand(core::PhaseId::kSceneMutation)
      , node_(node)
      , volume_(volume)
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) {
        return;
      }
      auto node = context.Scene->GetNode(node_);
      if (!node || !node->IsAlive()) {
        return;
      }
      auto impl = node->GetImpl();
      if (!impl) {
        return;
      }
      using scene::environment::LocalFogVolume;
      if (impl->get().HasComponent<LocalFogVolume>()) {
        impl->get().ReplaceComponent<LocalFogVolume>(volume_);
      } else {
        impl->get().AddComponent<LocalFogVolume>(volume_);
      }
      context.Scene->Update(false);
    }

  private:
    scene::NodeHandle node_;
    scene::environment::LocalFogVolume volume_;
  };

  //! Removes the local fog volume from one node, if it has one.
  class DetachLocalFogVolumeCommand final : public EditorCommand {
  public:
    explicit DetachLocalFogVolumeCommand(scene::NodeHandle node)
      : EditorCommand(core::PhaseId::kSceneMutation)
      , node_(node)
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) {
        return;
      }
      auto node = context.Scene->GetNode(node_);
      if (!node || !node->IsAlive()) {
        return;
      }
      auto impl = node->GetImpl();
      using scene::environment::LocalFogVolume;
      if (impl && impl->get().HasComponent<LocalFogVolume>()) {
        impl->get().RemoveComponent<LocalFogVolume>();
        context.Scene->Update(false);
      }
    }

  private:
    scene::NodeHandle node_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
