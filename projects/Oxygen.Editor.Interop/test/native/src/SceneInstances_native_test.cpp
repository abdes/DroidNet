//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#pragma managed(push, off)

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <EditorModule/NodeRegistry.h>
#include <EditorModule/SceneInstances.h>

#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneInstanceComponent.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

namespace {
using namespace oxygen::interop::module;
using oxygen::data::NodeId;
using oxygen::scene::NodeHandle;
using oxygen::scene::SceneNode;
using Instances = SceneInstances;

void Require(const bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

auto MakeId(const std::uint8_t seed) -> NodeId
{
  auto bytes = oxygen::data::StableIdBytes {};
  bytes.fill(seed);
  return NodeId::FromBytes(bytes);
}

auto MakeKey(const std::uint8_t seed) -> UuidKey
{
  auto key = UuidKey {};
  key.fill(seed);
  return key;
}

auto MakeChange(const std::uint8_t node, const float value)
  -> HostInstanceChange
{
  return HostInstanceChange {
    .path = { MakeId(node) },
    .override_id = oxygen::data::schema::OverrideId::kTransformPositionX,
    .part_id = {},
    .value = value,
  };
}

//! What the observers of one request saw.
struct Outcomes {
  std::vector<std::uint64_t> failures;
  std::vector<std::uint64_t> successes;
};

void Instantiate(Instances& instances, const NodeHandle root,
  std::vector<HostInstanceChange> changes, Outcomes& outcomes)
{
  instances.Instantiate(root, "/Content/Car.glb/Car.oscene",
    std::move(changes),
    [&outcomes](const std::uint64_t generation, const std::string&) {
      outcomes.failures.push_back(generation);
    },
    [&outcomes](const std::uint64_t generation) {
      outcomes.successes.push_back(generation);
    });
}

auto Root(const std::uint64_t index) -> NodeHandle
{
  return NodeHandle(static_cast<NodeHandle::IndexT>(index), 1U);
}

void ChangesBeforeTheBuildGoIntoIt()
{
  auto instances = Instances {};
  auto outcomes = Outcomes {};
  Instantiate(instances, Root(1), { MakeChange(1, 1.0F) }, outcomes);
  instances.SetChange(Root(1), MakeChange(1, 2.0F));
  instances.SetChange(Root(1), MakeChange(2, 3.0F));
  instances.ClearChange(Root(1), MakeChange(2, 0.0F).Address());

  const auto operations = instances.TakeOperations();
  Require(operations.size() == 1U, "one build carries every change");
  const auto& build = std::get<Instances::BuildOperation>(operations.front());
  Require(build.source == "/Content/Car.glb/Car.oscene", "build keeps source");
  Require(build.changes.size() == 1U
      && std::get<float>(build.changes.front().value) == 2.0F,
    "a change replaces the one with its address, a clear removes one");
  Require(build.generation != 0U, "a build takes a generation");
}

void ChangesAfterTheBuildAreSentAlone()
{
  auto instances = Instances {};
  auto outcomes = Outcomes {};
  Instantiate(instances, Root(1), {}, outcomes);
  (void)instances.TakeOperations();
  instances.SetChange(Root(1), MakeChange(1, 2.0F));
  instances.ClearChange(Root(1), MakeChange(1, 0.0F).Address());

  const auto operations = instances.TakeOperations();
  Require(operations.size() == 2U, "a set and a clear are sent");
  Require(std::holds_alternative<Instances::SetOperation>(operations[0])
      && std::holds_alternative<Instances::ClearOperation>(operations[1]),
    "operations keep request order");
}

void RefreshBuildsTheCurrentRequestAgain()
{
  auto instances = Instances {};
  auto outcomes = Outcomes {};
  Instantiate(instances, Root(1), {}, outcomes);
  (void)instances.TakeOperations();
  instances.SetChange(Root(1), MakeChange(1, 2.0F));
  (void)instances.TakeOperations();

  instances.Refresh();
  const auto operations = instances.TakeOperations();
  Require(operations.size() == 1U, "refresh builds every instance");
  const auto& build = std::get<Instances::BuildOperation>(operations.front());
  Require(build.changes.size() == 1U, "a rebuild keeps the changes set since");
}

void DetachForgetsTheRequest()
{
  auto instances = Instances {};
  auto outcomes = Outcomes {};
  Instantiate(instances, Root(1), {}, outcomes);
  instances.Detach(Root(1));

  const auto operations = instances.TakeOperations();
  Require(operations.size() == 1U
      && std::holds_alternative<Instances::DetachOperation>(operations[0]),
    "a detach drops the pending build");
  Require(!instances.Contains(Root(1)), "a detached root has no request");
  instances.Refresh();
  Require(instances.TakeOperations().empty(), "refresh does not rebuild it");
  instances.SetChange(Root(1), MakeChange(1, 2.0F));
  Require(instances.TakeOperations().empty(), "a change of no instance waits");
}

void OutcomesOlderThanTheLastBuildAreDropped()
{
  auto instances = Instances {};
  auto outcomes = Outcomes {};
  Instantiate(instances, Root(1), {}, outcomes);
  const auto first = std::get<Instances::BuildOperation>(
    instances.TakeOperations().front())
                       .generation;
  instances.Refresh();
  const auto second = std::get<Instances::BuildOperation>(
    instances.TakeOperations().front())
                        .generation;
  Require(second > first, "generations grow in execution order");

  instances.Report(Root(1), first, "stale");
  instances.Report(Root(1), second, "scene is not mounted");
  instances.Report(Root(1), second, {});
  Require(outcomes.failures == std::vector<std::uint64_t> { second },
    "only the current build's failure is reported");
  Require(outcomes.successes == std::vector<std::uint64_t> { second },
    "a success is reported");
}

void InstantiateAgainReplacesTheRequest()
{
  auto instances = Instances {};
  auto first = Outcomes {};
  auto second = Outcomes {};
  Instantiate(instances, Root(1), { MakeChange(1, 1.0F) }, first);
  Instantiate(instances, Root(1), {}, second);

  const auto operations = instances.TakeOperations();
  Require(operations.size() == 1U, "one build per root");
  const auto& build = std::get<Instances::BuildOperation>(operations.front());
  Require(build.changes.empty(), "the new request's changes are built");
  instances.Report(Root(1), build.generation, "failed");
  Require(first.failures.empty() && second.failures.size() == 1U,
    "outcomes reach the new request's observers");
}

void ForgetDropsADeadRoot()
{
  auto instances = Instances {};
  auto outcomes = Outcomes {};
  Instantiate(instances, Root(1), {}, outcomes);
  Instantiate(instances, Root(2), {}, outcomes);
  instances.Forget(Root(1));

  const auto operations = instances.TakeOperations();
  Require(operations.size() == 1U
      && std::get<Instances::BuildOperation>(operations.front()).root
        == Root(2),
    "a forgotten root's operations are dropped");
}

//! An instance root `root` that brought in `nodes`, by node ID.
void MakeInstanceRoot(
  SceneNode& root, std::unordered_map<NodeId, NodeHandle> nodes)
{
  auto impl = root.GetImpl();
  Require(impl.has_value(), "root has an implementation");
  impl->get().AddComponent<oxygen::scene::SceneInstanceComponent>(nullptr,
    nullptr, nullptr, std::vector<oxygen::data::SceneChange> {},
    std::move(nodes));
}

void ReferencesNameNestedInstancedNodes()
{
  NodeRegistry::ClearAll();
  auto scene = std::make_shared<oxygen::scene::Scene>("instances", 64U);
  auto root = scene->CreateNode("Car");
  auto body = *scene->CreateChildNode(root, "Body");
  auto wheel_root = *scene->CreateChildNode(body, "Wheel");
  auto rim = *scene->CreateChildNode(wheel_root, "Rim");
  auto owned = *scene->CreateChildNode(root, "Owned");
  MakeInstanceRoot(root,
    { { MakeId(1), body.GetHandle() }, { MakeId(2), wheel_root.GetHandle() } });
  MakeInstanceRoot(wheel_root, { { MakeId(3), rim.GetHandle() } });
  NodeRegistry::Register(MakeKey(9), root.GetHandle());
  NodeRegistry::Register(MakeKey(8), owned.GetHandle());

  const auto rim_reference
    = NodeReferenceKey { .node = MakeKey(9), .path = { MakeId(2), MakeId(3) } };
  Require(NodeRegistry::Lookup(*scene, rim_reference) == rim.GetHandle(),
    "a path finds each level through its instance root");
  Require(NodeRegistry::ReverseLookup(*scene, rim.GetHandle()) == rim_reference,
    "a nested node maps back to its reference");
  Require(NodeRegistry::ReverseLookup(*scene, wheel_root.GetHandle())
      == NodeReferenceKey { .node = MakeKey(9), .path = { MakeId(2) } },
    "a nested instance root is a node of the outer instance");
  Require(NodeRegistry::ReverseLookup(*scene, owned.GetHandle())
      == NodeReferenceKey { .node = MakeKey(8), .path = {} },
    "an owned node is its registered node");
  Require(!NodeRegistry::Lookup(*scene,
              NodeReferenceKey { .node = MakeKey(9), .path = { MakeId(7) } })
             .has_value(),
    "a path no instance holds names no node");
  NodeRegistry::ClearAll();
}

char last_error[512] {};

auto RunScenario(const int scenario) -> const char*
{
  try {
    switch (scenario) {
    case 0:
      ChangesBeforeTheBuildGoIntoIt();
      break;
    case 1:
      ChangesAfterTheBuildAreSentAlone();
      break;
    case 2:
      RefreshBuildsTheCurrentRequestAgain();
      break;
    case 3:
      DetachForgetsTheRequest();
      break;
    case 4:
      OutcomesOlderThanTheLastBuildAreDropped();
      break;
    case 5:
      InstantiateAgainReplacesTheRequest();
      break;
    case 6:
      ForgetDropsADeadRoot();
      break;
    case 7:
      ReferencesNameNestedInstancedNodes();
      break;
    default:
      throw std::runtime_error("unknown scenario");
    }
    return nullptr;
  } catch (const std::exception& ex) {
    strncpy_s(last_error, ex.what(), _TRUNCATE);
    return last_error;
  }
}
} // namespace

#pragma managed(pop)

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;

namespace InteropTests {

//! The instances the editor builds keep their requests, run their operations
//! in order and report only current outcomes; node references name instanced
//! nodes at every nesting level.
[TestClass] public ref class SceneInstancesTests {
private:
  static void Check(int scenario)
  {
    const auto error = RunScenario(scenario);
    Assert::IsTrue(
      error == nullptr, gcnew System::String(error != nullptr ? error : ""));
  }

public:
  [TestMethod] void ChangesBeforeTheBuildGoIntoIt() { Check(0); }

  [TestMethod] void ChangesAfterTheBuildAreSentAlone() { Check(1); }

  [TestMethod] void RefreshBuildsTheCurrentRequestAgain() { Check(2); }

  [TestMethod] void DetachForgetsTheRequest() { Check(3); }

  [TestMethod] void OutcomesOlderThanTheLastBuildAreDropped() { Check(4); }

  [TestMethod] void InstantiateAgainReplacesTheRequest() { Check(5); }

  [TestMethod] void ForgetDropsADeadRoot() { Check(6); }

  [TestMethod] void ReferencesNameNestedInstancedNodes() { Check(7); }
};

} // namespace InteropTests
