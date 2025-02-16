//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Detail/ProxyFrame.h>
#include <Oxygen/OxCo/Detail/TaskFrame.h>

#include <coroutine>

#include <Oxygen/Testing/GTest.h>

using namespace oxygen::co::detail;

namespace {

class ProxyFrameTest : public testing::Test {
protected:
    ProxyFrame proxy_frame_;
    TaskFrame task_frame_;
};

void DummyDestroyFn(CoroutineFrame*) { }

NOLINT_TEST_F(ProxyFrameTest, LinkToAndFollowLink)
{
    CoroutineFrame frame;
    frame.destroy_fn = DummyDestroyFn;
    const Handle handle = frame.ToHandle();

    proxy_frame_.LinkTo(handle);
    const Handle linked_handle = proxy_frame_.FollowLink();

    EXPECT_EQ(linked_handle.address(), handle.address());
}

NOLINT_TEST_F(ProxyFrameTest, IsTagged)
{
    EXPECT_TRUE(ProxyFrame::IsTagged(ProxyFrame::kTag, &proxy_frame_));
    EXPECT_TRUE(ProxyFrame::IsTagged(TaskFrame::kTag, &task_frame_));
}

NOLINT_TEST_F(ProxyFrameTest, TaskFrameProgramCounter)
{
    constexpr uintptr_t pc = 42;
    task_frame_.ProgramCounter(pc);
    EXPECT_EQ(task_frame_.ProgramCounter(), pc);
}

NOLINT_TEST_F(ProxyFrameTest, FrameCast)
{
    CoroutineFrame* frame = &task_frame_;
    auto* cast_frame = FrameCast<TaskFrame>(frame);
    EXPECT_EQ(cast_frame, &task_frame_);

    CoroutineFrame non_task_frame;
    non_task_frame.destroy_fn = DummyDestroyFn;
    EXPECT_EQ(FrameCast<TaskFrame>(&non_task_frame), nullptr);
}

} // namespace
