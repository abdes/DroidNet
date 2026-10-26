//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch_clr.h"

#pragma managed(push, off)
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/EditorInterface/EngineContext.h>
#include <Oxygen/Engine/AsyncEngine.h>

#include <Commands/ReadSceneHierarchyCommand.h>
#include <EditorModule/EditorModule.h>
#pragma managed(pop)

#pragma managed

#include <msclr/gcroot.h>

#include <Utils/Utf8Marshaling.h>
#include <World/OxygenWorld.h>

using namespace System::Threading::Tasks;
using namespace oxygen::interop::module;

namespace Oxygen::Interop::World {
namespace {

using oxygen::interop::FromUtf8;
using oxygen::interop::ToUtf8;
using Hierarchy = oxygen::content::IAssetLoader::SceneHierarchy;

// Asset keys and node IDs keep the canonical text's byte order.
auto ToGuid(const std::string& canonical) -> System::Guid
{
  return System::Guid::ParseExact(FromUtf8(canonical), "D");
}

auto ToManaged(const Hierarchy& hierarchy) -> SceneHierarchyManaged
{
  SceneHierarchyManaged result;
  result.Exists = true;
  result.SceneKey = ToGuid(oxygen::data::to_string(hierarchy.scene_key));
  result.Nodes = gcnew array<SceneHierarchyNodeManaged>(
    static_cast<int>(hierarchy.nodes.size()));
  for (int index = 0; index < result.Nodes->Length; ++index) {
    const auto& node = hierarchy.nodes.at(static_cast<std::size_t>(index));
    SceneHierarchyNodeManaged entry;
    entry.Path = gcnew array<System::Guid>(static_cast<int>(node.path.size()));
    for (int level = 0; level < entry.Path->Length; ++level) {
      entry.Path[level] = ToGuid(oxygen::data::to_string(
        node.path.at(static_cast<std::size_t>(level))));
    }
    entry.Name = FromUtf8(node.name);
    entry.Parent
      = node.parent.has_value() ? static_cast<int>(*node.parent) : -1;
    entry.SceneKey = ToGuid(oxygen::data::to_string(node.scene_key));
    entry.InstanceKey = node.instance.has_value()
      ? ToGuid(oxygen::data::to_string(*node.instance))
      : System::Guid::Empty;
    result.Nodes[index] = entry;
  }
  result.Issues = gcnew array<SceneHierarchyIssueManaged>(
    static_cast<int>(hierarchy.issues.size()));
  for (int index = 0; index < result.Issues->Length; ++index) {
    const auto& issue = hierarchy.issues.at(static_cast<std::size_t>(index));
    SceneHierarchyIssueManaged entry;
    entry.Code = static_cast<SceneHierarchyIssueCodeManaged>(issue.code);
    entry.Node = issue.node.has_value() ? static_cast<int>(*issue.node) : -1;
    entry.SceneKey = ToGuid(oxygen::data::to_string(issue.scene_key));
    result.Issues[index] = entry;
  }
  return result;
}

class SceneHierarchyCompletion final {
public:
  explicit SceneHierarchyCompletion(
    TaskCompletionSource<SceneHierarchyManaged>^ completion)
    : completion_(completion)
  {
  }

  ~SceneHierarchyCompletion() { completion_->TrySetCanceled(); }

  OXYGEN_MAKE_NON_COPYABLE(SceneHierarchyCompletion)
  OXYGEN_MAKE_NON_MOVABLE(SceneHierarchyCompletion)

  void Complete(const std::optional<Hierarchy>& hierarchy) const
  {
    if (!hierarchy.has_value()) {
      SceneHierarchyManaged missing;
      missing.Exists = false;
      missing.Nodes = gcnew array<SceneHierarchyNodeManaged>(0);
      missing.Issues = gcnew array<SceneHierarchyIssueManaged>(0);
      completion_->TrySetResult(missing);
      return;
    }
    completion_->TrySetResult(ToManaged(*hierarchy));
  }

private:
  msclr::gcroot<TaskCompletionSource<SceneHierarchyManaged>^> completion_;
};

auto MakeSceneHierarchyCallback(
  TaskCompletionSource<SceneHierarchyManaged>^ completion)
  -> ReadSceneHierarchyCommand::Callback
{
  // The same ownership as the node observation's completion, which holds a
  // managed handle.
  auto observer = std::shared_ptr<SceneHierarchyCompletion>(
    new SceneHierarchyCompletion(completion));
  return [observer](std::optional<Hierarchy> hierarchy) -> void {
    observer->Complete(hierarchy);
  };
}

} // namespace

Task<SceneHierarchyManaged>^ OxygenWorld::ReadSceneHierarchyAsync(
  String^ virtualPath)
{
  if (virtualPath == nullptr) {
    throw gcnew System::ArgumentNullException("virtualPath");
  }
  auto native_context = context_->NativePtr();
  if (!native_context || !native_context->engine) {
    throw gcnew System::InvalidOperationException(
      "Scene hierarchy reading has no engine context.");
  }
  auto module = native_context->engine->GetModule<EditorModule>();
  if (!module) {
    throw gcnew System::InvalidOperationException(
      "Scene hierarchy reading has no editor module.");
  }
  auto completion = gcnew TaskCompletionSource<SceneHierarchyManaged>(
    TaskCreationOptions::RunContinuationsAsynchronously);
  module->get().Enqueue(std::make_unique<ReadSceneHierarchyCommand>(
    ToUtf8(virtualPath), MakeSceneHierarchyCallback(completion)));
  return completion->Task;
}

} // namespace Oxygen::Interop::World
