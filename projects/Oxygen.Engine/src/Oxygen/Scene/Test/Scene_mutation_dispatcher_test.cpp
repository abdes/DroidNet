//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Internal/IMutationCollector.h>
#include <Oxygen/Scene/Internal/MutationCollector.h>
#include <Oxygen/Scene/Internal/MutationDispatcher.h>
#include <Oxygen/Scene/Internal/ScriptSlotMutationProcessor.h>

namespace {

using oxygen::observer_ptr;
using oxygen::scene::NodeHandle;
using oxygen::scene::ScriptSlotIndex;
using oxygen::scene::internal::CameraMutation;
using oxygen::scene::internal::IScriptSlotMutationProcessor;
using oxygen::scene::internal::LightMutation;
using oxygen::scene::internal::ScriptSlotMutation;
using oxygen::scene::internal::ScriptSlotMutationType;

class TestScriptSlotProcessor final : public IScriptSlotMutationProcessor {
public:
  auto Process(const ScriptSlotMutation& mutation,
    const ResolveScriptSlotFn& /*resolve_slot*/,
    const NotifyObserversFn& /*notify_observers*/) -> void override
  {
    seen.push_back(mutation);
  }

  auto QueueTrackedSlotDeactivations(
    oxygen::scene::internal::IMutationCollector& /*mutation_collector*/) const
    -> void override
  {
  }

  std::vector<ScriptSlotMutation> seen {};
};

NOLINT_TEST(SceneMutationDispatcherTest,
  PreservesScriptOrderAndCoalescesCameraAndLightMutations)
{
  auto collector = oxygen::scene::internal::CreateMutationCollector();
  ASSERT_NE(collector, nullptr);

  TestScriptSlotProcessor script_processor;
  auto dispatcher = oxygen::scene::internal::CreateMutationDispatcher(
    observer_ptr { &script_processor });
  ASSERT_NE(dispatcher, nullptr);

  const NodeHandle script_node { 1, 1 };
  const NodeHandle light_node { 2, 1 };
  const NodeHandle camera_node { 3, 1 };
  const auto script_slot = ScriptSlotIndex { 0 };

  collector->CollectScriptSlotActivated(script_node, script_slot);
  collector->CollectLightChanged(light_node);
  collector->CollectLightChanged(light_node);
  collector->CollectCameraChanged(camera_node);
  collector->CollectCameraChanged(camera_node);
  collector->CollectScriptSlotChanged(script_node, script_slot);

  std::vector<LightMutation> light_notifications;
  std::vector<CameraMutation> camera_notifications;
  dispatcher->Dispatch(*collector,
    {
      .resolve_script_slot = {},
      .notify_script_observers = {},
      .notify_light_mutation =
        [&light_notifications](const LightMutation& mutation) {
          light_notifications.push_back(mutation);
        },
      .notify_camera_mutation =
        [&camera_notifications](const CameraMutation& mutation) {
          camera_notifications.push_back(mutation);
        },
    });

  ASSERT_EQ(script_processor.seen.size(), 2U);
  EXPECT_EQ(script_processor.seen[0].type, ScriptSlotMutationType::kActivated);
  EXPECT_EQ(script_processor.seen[1].type, ScriptSlotMutationType::kChanged);
  EXPECT_EQ(script_processor.seen[0].node_handle, script_node);
  EXPECT_EQ(script_processor.seen[0].slot_index, script_slot);
  EXPECT_EQ(script_processor.seen[1].node_handle, script_node);
  EXPECT_EQ(script_processor.seen[1].slot_index, script_slot);

  ASSERT_EQ(light_notifications.size(), 1U);
  EXPECT_EQ(light_notifications[0].node_handle, light_node);

  ASSERT_EQ(camera_notifications.size(), 1U);
  EXPECT_EQ(camera_notifications[0].node_handle, camera_node);

  const auto counters = dispatcher->GetCounters();
  EXPECT_EQ(counters.sync_calls, 1U);
  EXPECT_EQ(counters.frames_with_mutations, 1U);
  EXPECT_EQ(counters.drained_records, 6U);
  EXPECT_EQ(counters.script_records_dispatched, 2U);
  EXPECT_EQ(counters.light_records_coalesced_in, 2U);
  EXPECT_EQ(counters.light_records_dispatched, 1U);
  EXPECT_EQ(counters.camera_records_coalesced_in, 2U);
  EXPECT_EQ(counters.camera_records_dispatched, 1U);
}

} // namespace
