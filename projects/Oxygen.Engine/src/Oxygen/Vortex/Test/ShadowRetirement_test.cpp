//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <utility>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Queues.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#  include <Oxygen/Graphics/Common/Test/HeapAllocationFailure.h>
#endif
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowRequest.h>
#include <Oxygen/Vortex/Shadows/Internal/SharedShadowMap.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace {
NOLINT_TEST(ShadowRetirementTest, RecordedUseDefersSlotReuseWithoutAllocation)
{
  using oxygen::graphics::QueueRole;
  using oxygen::vortex::Renderer;
  namespace shadows = oxygen::vortex::shadows::internal;
  auto graphics = std::make_shared<oxygen::vortex::testing::FakeGraphics>();
  const auto strategy = oxygen::graphics::SingleQueueStrategy {};
  graphics->CreateCommandQueues(strategy);
  auto config = oxygen::RendererConfig {};
  config.upload_queue_key = graphics->QueueKeyFor(QueueRole::kGraphics).get();
  auto renderer = std::make_unique<Renderer>(graphics, std::move(config));
  const oxygen::ScopeGuard shutdown(
    [&] noexcept -> void { renderer->OnShutdown(); });
  auto allocator = shadows::ConventionalShadowTargetAllocator(*renderer);
  auto request = shadows::LocalShadowRequest {};
  request.content.light = oxygen::scene::NodeHandle(1U, 1U);
  request.content.resolution = 16U;
  request.content.scene_generation = 1U;
  const auto view = oxygen::ViewId { 1U };

  for (const bool submitted : { false, true }) {
    auto acquired = allocator.AcquireLocalMap(view, request);
    const auto original = acquired.owner->version->slot->handle;
    auto observed = std::weak_ptr(acquired.owner->version->slot);
    auto recording = graphics->AcquireCommandRecorder(
      graphics->QueueKeyFor(QueueRole::kGraphics), "Retirement test writer",
      oxygen::graphics::SubmissionPolicy::kExplicit);
    shadows::AttachShadowUse(acquired.owner->version,
      shadows::ShadowUseMode::kWrite, *recording,
      graphics->GetResourceRegistry());
    // Keep the fake command-list object alive outside the denial scope;
    // the owner/use callbacks must still release the shadow slot inside it.
    const auto command_list = recording->GetCommandListForInspection();
    acquired.Commit();
    auto owner = std::move(acquired.owner);
    allocator.RetainLocalSources(view, 1U, {});
    auto queue = graphics->GetFakeCommandQueue(QueueRole::kGraphics);
    ASSERT_NE(queue, nullptr);
    queue->SetAutoComplete(false);
    if (submitted) {
      ASSERT_TRUE(recording.Submit());
    }
    bool retained_after_owner_release = false;
#if defined(_MSC_VER) && defined(_DEBUG)
    const auto rejected
      = oxygen::graphics::testing::HeapAllocationFailure::RejectedCount();
    {
      const oxygen::graphics::testing::HeapAllocationFailure denied;
#endif
      owner.reset();
      retained_after_owner_release = !observed.expired();
      if (submitted) {
        graphics->FlushCommandQueues();
      } else {
        recording.Discard();
      }
#if defined(_MSC_VER) && defined(_DEBUG)
    }
    EXPECT_EQ(oxygen::graphics::testing::HeapAllocationFailure::RejectedCount(),
      rejected);
#endif
    EXPECT_TRUE(retained_after_owner_release);
    EXPECT_TRUE(observed.expired());
    auto next = allocator.AcquireLocalMap(view, request);
    EXPECT_EQ(next.owner->version->slot->handle.index, original.index);
    EXPECT_NE(
      next.owner->version->slot->handle.generation, original.generation);
  }
}
} // namespace
