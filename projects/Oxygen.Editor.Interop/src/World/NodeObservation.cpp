//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include <functional>
#include <memory>
#include <string_view>
#include <msclr/gcroot.h>

#include <Oxygen/EditorInterface/EngineContext.h>
#include <Oxygen/Engine/AsyncEngine.h>

#include <Commands/ObserveNodeCommand.h>
#include <EditorModule/EditorModule.h>
#include <World/OxygenWorld.h>

using namespace System::Threading::Tasks;
using namespace oxygen::interop::module;

namespace Oxygen::Interop::World {
namespace {

auto FromUtf8(std::string_view text) -> System::String^
{
  auto bytes = gcnew array<System::Byte>(static_cast<int>(text.size()));
  for (int index = 0; index < bytes->Length; ++index) {
    bytes[index] = static_cast<System::Byte>(text[static_cast<std::size_t>(index)]);
  }
  return System::Text::Encoding::UTF8->GetString(bytes);
}

class NodeObservationCompletion final {
public:
  explicit NodeObservationCompletion(TaskCompletionSource<NodeStateManaged>^ completion)
    : completion_(completion) {}

  ~NodeObservationCompletion() { completion_->TrySetCanceled(); }

  void Complete(const NodeObservation& value) const
  {
    NodeStateManaged result;
    result.Exists = value.exists;
    result.IsPrimarySun = value.primary_sun;
    result.GeometryKey = FromUtf8(value.geometry_key);
    result.GeometryName = FromUtf8(value.geometry_name);
    result.VertexCount = value.vertex_count;
    result.IndexCount = value.index_count;
    result.Properties = gcnew array<PropertyValueEntry>(static_cast<int>(value.properties.size()));
    for (int index = 0; index < result.Properties->Length; ++index) {
      const auto& property = value.properties[static_cast<std::size_t>(index)];
      PropertyValueEntry entry;
      entry.ComponentId = static_cast<System::UInt16>(property.component);
      entry.FieldId = property.field;
      entry.Value = property.value;
      result.Properties[index] = entry;
    }
    result.MaterialKeys = gcnew array<System::String^>(static_cast<int>(value.material_keys.size()));
    result.MaterialBaseColors = gcnew array<System::Numerics::Vector4>(static_cast<int>(value.material_base_colors.size()));
    for (int index = 0; index < result.MaterialKeys->Length; ++index) {
      result.MaterialKeys[index] = FromUtf8(value.material_keys[static_cast<std::size_t>(index)]);
      const auto& color = value.material_base_colors[static_cast<std::size_t>(index)];
      result.MaterialBaseColors[index] = System::Numerics::Vector4(color[0], color[1], color[2], color[3]);
    }
    completion_->TrySetResult(result);
  }

private:
  msclr::gcroot<TaskCompletionSource<NodeStateManaged>^> completion_;
};

auto MakeNodeObservationCallback(TaskCompletionSource<NodeStateManaged>^ completion)
  -> std::function<void(NodeObservation)>
{
  auto observer = std::make_shared<NodeObservationCompletion>(completion);
  return [observer](NodeObservation value) { observer->Complete(value); };
}

} // namespace

Task<NodeStateManaged>^ OxygenWorld::ObserveNodeAsync(System::Guid nodeId)
{
  auto native_context = context_->NativePtr();
  if (!native_context || !native_context->engine) {
    throw gcnew System::InvalidOperationException("Node observation has no engine context.");
  }
  auto module = native_context->engine->GetModule<EditorModule>();
  if (!module) {
    throw gcnew System::InvalidOperationException("Node observation has no editor module.");
  }
  auto bytes = nodeId.ToByteArray();
  UuidKey key {};
  for (int index = 0; index < 16; ++index) {
    key[static_cast<std::size_t>(index)] = bytes[index];
  }
  auto completion = gcnew TaskCompletionSource<NodeStateManaged>(
    TaskCreationOptions::RunContinuationsAsynchronously);
  module->get().Enqueue(std::make_unique<ObserveNodeCommand>(
    key, MakeNodeObservationCallback(completion)));
  return completion->Task;
}

} // namespace Oxygen::Interop::World
