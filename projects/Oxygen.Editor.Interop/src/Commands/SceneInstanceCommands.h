//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/SceneInstances.h>

namespace oxygen::interop::module {

namespace detail {
  //! The scene instances of a command whose node is alive; null when the
  //! node is gone.
  inline auto InstancesFor(CommandContext& context, scene::NodeHandle node)
    -> SceneInstances*
  {
    if (!context.Scene) {
      return nullptr;
    }
    const auto scene_node = context.Scene->GetNode(node);
    if (!scene_node || !scene_node->IsAlive()) {
      return nullptr;
    }
    if (!context.Instances) {
      throw std::logic_error("Instance command requires scene instance state");
    }
    return context.Instances.get();
  }
} // namespace detail

//! Instances a cooked scene under a node, with the editor's changes; replaces
//! an instance the node has.
class InstantiateSceneCommand final : public EditorCommand {
public:
  InstantiateSceneCommand(scene::NodeHandle root, std::string source,
    std::vector<HostInstanceChange> changes,
    SceneInstances::FailureCallback on_failure,
    SceneInstances::SuccessCallback on_success)
    : EditorCommand(core::PhaseId::kSceneMutation)
    , root_(root)
    , source_(std::move(source))
    , changes_(std::move(changes))
    , on_failure_(std::move(on_failure))
    , on_success_(std::move(on_success))
  {
  }

  void Execute(CommandContext& context) override
  {
    if (auto* instances = detail::InstancesFor(context, root_)) {
      instances->Instantiate(root_, std::move(source_), std::move(changes_),
        std::move(on_failure_), std::move(on_success_));
    }
  }

private:
  scene::NodeHandle root_;
  std::string source_;
  std::vector<HostInstanceChange> changes_;
  SceneInstances::FailureCallback on_failure_;
  SceneInstances::SuccessCallback on_success_;
};

//! Sets one change of a node's instance.
class SetInstanceChangeCommand final : public EditorCommand {
public:
  SetInstanceChangeCommand(scene::NodeHandle root, HostInstanceChange change)
    : EditorCommand(core::PhaseId::kSceneMutation)
    , root_(root)
    , change_(std::move(change))
  {
  }

  void Execute(CommandContext& context) override
  {
    if (auto* instances = detail::InstancesFor(context, root_)) {
      instances->SetChange(root_, std::move(change_));
    }
  }

private:
  scene::NodeHandle root_;
  HostInstanceChange change_;
};

//! Clears one change of a node's instance: the node takes the value of the
//! next level, or of its source.
class ClearInstanceChangeCommand final : public EditorCommand {
public:
  ClearInstanceChangeCommand(
    scene::NodeHandle root, data::ChangeAddress address)
    : EditorCommand(core::PhaseId::kSceneMutation)
    , root_(root)
    , address_(std::move(address))
  {
  }

  void Execute(CommandContext& context) override
  {
    if (auto* instances = detail::InstancesFor(context, root_)) {
      instances->ClearChange(root_, address_);
    }
  }

private:
  scene::NodeHandle root_;
  data::ChangeAddress address_;
};

//! Removes a node's instance; the node and its owned children stay.
class DetachInstanceCommand final : public EditorCommand {
public:
  explicit DetachInstanceCommand(scene::NodeHandle root)
    : EditorCommand(core::PhaseId::kSceneMutation)
    , root_(root)
  {
  }

  void Execute(CommandContext& context) override
  {
    if (auto* instances = detail::InstancesFor(context, root_)) {
      instances->Detach(root_);
    }
  }

private:
  scene::NodeHandle root_;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
