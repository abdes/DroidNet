//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch_clr.h"

#pragma managed(push, off)
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/EditorInterface/EngineContext.h>
#include <Oxygen/Engine/AsyncEngine.h>

#include <Commands/ReadSceneHierarchyCommand.h>
#include <Commands/ReadSceneNodeCommand.h>
#include <EditorModule/EditorModule.h>
#pragma managed(pop)

#pragma managed

#include <msclr/gcroot.h>

#include <Utils/Utf8Marshaling.h>
#include <Views/NodeReferenceManaged.h>
#include <World/OxygenWorld.h>

using namespace System::Threading::Tasks;
using namespace oxygen::interop::module;

namespace Oxygen::Interop::World {
namespace {

using oxygen::interop::FromUtf8;
using oxygen::interop::ToUtf8;
using Hierarchy = oxygen::content::IAssetLoader::SceneHierarchy;
using NodeValues = oxygen::content::IAssetLoader::SceneNodeValues;
using NodeValue = oxygen::content::IAssetLoader::SceneNodeValue;
using NodeAsset = oxygen::content::IAssetLoader::SceneNodeAsset;
using LodPolicyRecord = oxygen::data::pak::geometry::LodPolicyRecord;

// Asset keys and node IDs keep the canonical text's byte order.
auto ToGuid(const std::string& canonical) -> System::Guid
{
  return System::Guid::ParseExact(FromUtf8(canonical), "D");
}

auto ToManaged(const Hierarchy& hierarchy) -> SceneHierarchyManaged
{
  SceneHierarchyManaged result{};
  result.Exists = true;
  result.SceneKey = ToGuid(oxygen::data::to_string(hierarchy.scene_key));
  result.Nodes = gcnew array<SceneHierarchyNodeManaged>(
    static_cast<int>(hierarchy.nodes.size()));
  for (int index = 0; index < result.Nodes->Length; ++index) {
    const auto& node = hierarchy.nodes.at(static_cast<std::size_t>(index));
    SceneHierarchyNodeManaged entry{};
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
    entry.GeometryPath = node.geometry.has_value()
      ? FromUtf8(node.geometry->virtual_path)
      : nullptr;
    entry.HasCamera = node.camera;
    result.Nodes[index] = entry;
  }
  result.Issues = gcnew array<SceneHierarchyIssueManaged>(
    static_cast<int>(hierarchy.issues.size()));
  for (int index = 0; index < result.Issues->Length; ++index) {
    const auto& issue = hierarchy.issues.at(static_cast<std::size_t>(index));
    SceneHierarchyIssueManaged entry{};
    entry.Code = static_cast<SceneHierarchyIssueCodeManaged>(issue.code);
    entry.Node = issue.node.has_value() ? static_cast<int>(*issue.node) : -1;
    entry.SceneKey = ToGuid(oxygen::data::to_string(issue.scene_key));
    result.Issues[index] = entry;
  }
  return result;
}

auto MissingHierarchy() -> SceneHierarchyManaged
{
  SceneHierarchyManaged missing{};
  missing.Exists = false;
  missing.Nodes = gcnew array<SceneHierarchyNodeManaged>(0);
  missing.Issues = gcnew array<SceneHierarchyIssueManaged>(0);
  return missing;
}

auto ToBoundaries(const float (&boundaries)[oxygen::data::pak::geometry::
    kMaxLodBoundaryCount],
  const std::size_t count) -> array<float>^
{
  auto managed = gcnew array<float>(static_cast<int>(count));
  for (int index = 0; index < managed->Length; ++index) {
    managed[index] = boundaries[index];
  }
  return managed;
}

// Only the boundaries of the policy's own type hold values; the record's
// other boundary arrays are unused.
auto ToManaged(const LodPolicyRecord& record) -> LodPolicyManaged
{
  using oxygen::data::pak::geometry::LodPolicyType;
  const auto distance = record.type == LodPolicyType::kDistance;
  const auto error = record.type == LodPolicyType::kScreenSpaceError;
  LodPolicyManaged policy{};
  policy.Type = static_cast<System::Byte>(record.type);
  policy.LodIndex = record.lod_index;
  policy.HysteresisRatio = record.hysteresis_ratio;
  policy.Thresholds
    = ToBoundaries(record.thresholds, distance ? record.boundary_count : 0U);
  policy.EnterFinerSse
    = ToBoundaries(record.enter_finer_sse, error ? record.boundary_count : 0U);
  policy.ExitCoarserSse = ToBoundaries(
    record.exit_coarser_sse, error ? record.boundary_count : 0U);
  return policy;
}

auto ToManaged(const NodeValue& value, SceneNodePropertyManaged% property)
  -> void
{
  if (const auto* flag = std::get_if<bool>(&value)) {
    property.Kind = InstanceValueKind::Bool;
    property.BoolValue = *flag;
  } else if (const auto* number = std::get_if<std::int32_t>(&value)) {
    property.Kind = InstanceValueKind::Int32;
    property.Int32Value = *number;
  } else if (const auto* scalar = std::get_if<float>(&value)) {
    property.Kind = InstanceValueKind::Float;
    property.FloatValue = *scalar;
  } else if (const auto* vector = std::get_if<std::array<float, 3>>(&value)) {
    property.Kind = InstanceValueKind::Vector3;
    property.Vector3Value
      = System::Numerics::Vector3(vector->at(0), vector->at(1), vector->at(2));
  } else if (const auto* asset = std::get_if<NodeAsset>(&value)) {
    property.Kind = InstanceValueKind::Asset;
    property.AssetPath = FromUtf8(asset->virtual_path);
  } else if (const auto* policy = std::get_if<LodPolicyRecord>(&value)) {
    property.Kind = InstanceValueKind::LodPolicy;
    property.LodPolicy = ToManaged(*policy);
  }
}

auto ToManaged(const NodeValues& values) -> SceneNodeValuesManaged
{
  SceneNodeValuesManaged result{};
  result.Exists = true;
  result.Name = FromUtf8(values.name);
  result.GeometryPath = values.geometry.has_value()
    ? FromUtf8(values.geometry->virtual_path)
    : nullptr;
  result.Properties = gcnew array<SceneNodePropertyManaged>(
    static_cast<int>(values.properties.size()));
  for (int index = 0; index < result.Properties->Length; ++index) {
    const auto& property
      = values.properties.at(static_cast<std::size_t>(index));
    SceneNodePropertyManaged entry{};
    entry.OverrideId = static_cast<System::UInt16>(property.override_id);
    entry.PartId = property.part_id.IsNil()
      ? System::Guid::Empty
      : ToGuid(oxygen::data::to_string(property.part_id));
    entry.LayoutRevision = oxygen::base::IsAllZero(property.layout_revision)
      ? nullptr
      : FromUtf8(oxygen::base::ToHexString(property.layout_revision));
    ToManaged(property.value, entry);
    result.Properties[index] = entry;
  }
  return result;
}

auto MissingNode() -> SceneNodeValuesManaged
{
  SceneNodeValuesManaged missing{};
  missing.Exists = false;
  missing.Properties = gcnew array<SceneNodePropertyManaged>(0);
  return missing;
}

//! Completes a managed task with the result of an engine read, or cancels it
//! when the read is dropped without running.
template <typename Result> class ReadCompletion final {
public:
  explicit ReadCompletion(TaskCompletionSource<Result>^ completion)
    : completion_(completion)
  {
  }

  ~ReadCompletion() { completion_->TrySetCanceled(); }

  OXYGEN_MAKE_NON_COPYABLE(ReadCompletion)
  OXYGEN_MAKE_NON_MOVABLE(ReadCompletion)

  void Complete(Result result) const { completion_->TrySetResult(result); }

private:
  msclr::gcroot<TaskCompletionSource<Result>^> completion_;
};

auto MakeSceneHierarchyCallback(
  TaskCompletionSource<SceneHierarchyManaged>^ completion)
  -> ReadSceneHierarchyCommand::Callback
{
  // The same ownership as the node observation's completion, which holds a
  // managed handle.
  auto observer = std::shared_ptr<ReadCompletion<SceneHierarchyManaged>>(
    new ReadCompletion<SceneHierarchyManaged>(completion));
  return [observer](std::optional<Hierarchy> hierarchy) -> void {
    observer->Complete(
      hierarchy.has_value() ? ToManaged(*hierarchy) : MissingHierarchy());
  };
}

auto MakeSceneNodeCallback(
  TaskCompletionSource<SceneNodeValuesManaged>^ completion)
  -> ReadSceneNodeCommand::Callback
{
  auto observer = std::shared_ptr<ReadCompletion<SceneNodeValuesManaged>>(
    new ReadCompletion<SceneNodeValuesManaged>(completion));
  return [observer](std::optional<NodeValues> values) -> void {
    observer->Complete(values.has_value() ? ToManaged(*values) : MissingNode());
  };
}

auto RequireEditorModule(
  const oxygen::engine::interop::EngineContext* native_context,
  String^ operation) -> EditorModule&
{
  if (!native_context || !native_context->engine) {
    throw gcnew System::InvalidOperationException(
      operation + " has no engine context.");
  }
  auto module = native_context->engine->GetModule<EditorModule>();
  if (!module) {
    throw gcnew System::InvalidOperationException(
      operation + " has no editor module.");
  }
  return module->get();
}

} // namespace

Task<SceneHierarchyManaged>^ OxygenWorld::ReadSceneHierarchyAsync(
  String^ virtualPath)
{
  if (virtualPath == nullptr) {
    throw gcnew System::ArgumentNullException("virtualPath");
  }
  auto& module
    = RequireEditorModule(context_->NativePtr(), "Scene hierarchy reading");
  auto completion = gcnew TaskCompletionSource<SceneHierarchyManaged>(
    TaskCreationOptions::RunContinuationsAsynchronously);
  module.Enqueue(std::make_unique<ReadSceneHierarchyCommand>(
    ToUtf8(virtualPath), MakeSceneHierarchyCallback(completion)));
  return completion->Task;
}

Task<SceneNodeValuesManaged>^ OxygenWorld::ReadSceneNodeAsync(
  String^ virtualPath, array<System::Guid>^ path)
{
  if (virtualPath == nullptr) {
    throw gcnew System::ArgumentNullException("virtualPath");
  }
  if (path == nullptr || path->Length == 0) {
    throw gcnew System::ArgumentException(
      "A scene node read names a node of the scene.", "path");
  }
  auto native_path = std::vector<oxygen::data::NodeId> {};
  for each (System::Guid id in path) {
    const auto parsed = ::Oxygen::Interop::detail::ToNativeNodeId(id);
    if (!parsed.has_value()) {
      throw gcnew System::ArgumentException(
        "A node path holds an empty node ID.", "path");
    }
    native_path.push_back(*parsed);
  }
  auto& module = RequireEditorModule(context_->NativePtr(), "Scene node reading");
  auto completion = gcnew TaskCompletionSource<SceneNodeValuesManaged>(
    TaskCreationOptions::RunContinuationsAsynchronously);
  module.Enqueue(std::make_unique<ReadSceneNodeCommand>(ToUtf8(virtualPath),
    std::move(native_path), MakeSceneNodeCallback(completion)));
  return completion->Task;
}

} // namespace Oxygen::Interop::World
