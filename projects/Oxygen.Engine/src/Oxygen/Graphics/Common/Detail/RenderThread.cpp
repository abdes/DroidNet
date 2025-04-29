//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <mutex>
#include <optional>
#include <queue>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Detail/RenderThread.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/ParkingLot.h>
#include <Oxygen/OxCo/Run.h>

using oxygen::graphics::FrameRenderTask;
using oxygen::graphics::detail::RenderThread;

namespace {
class RenderTaskDispatcher {
public:
    RenderTaskDispatcher(uint32_t frames_in_flight)
        : frames_in_flight_(frames_in_flight)
    {
        DCHECK_GT_F(frames_in_flight_, 0UL, "The number of frames in flight must be > 0");
    }

    ~RenderTaskDispatcher() { Stop(); }

    void Stop()
    {
        if (!running_) {
            return;
        }
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_work_queue_);
            cv_ready_.notify_all();
        }
    }

    auto IsRunning() const -> bool { return running_; }

    // Called from the game/main thread to submit a frame for rendering.
    void Submit(FrameRenderTask task)
    {
        std::unique_lock<std::mutex> lock(mutex_work_queue_);
        // Wait if the queue is full (frame lag)
        cv_ready_.wait(lock, [&] { return work_queue_.size() < static_cast<size_t>(frames_in_flight_) || !running_; });
        work_queue_.emplace(std::move(task));
        cv_ready_.notify_all();
    }

    // Wait for and retrieve the next render task
    auto GetNextTask() -> std::optional<FrameRenderTask>
    {
        std::unique_lock<std::mutex> lock(mutex_work_queue_);

        CHECK_F(!work_queue_.empty(), "GetNextTask() called with empty task queue");

        // Get the next task
        auto task = std::move(work_queue_.front());
        work_queue_.pop();
        cv_ready_.notify_all();
        return task;
    }

    auto WorkAvailable()
    {
        return work_available_.Park();
    }

private:
    template <typename T, typename>
    friend struct oxygen::co::EventLoopTraits;

    void EventLoop()
    {
        running_ = true;
        while (running_) {
            std::unique_lock<std::mutex> lock(mutex_work_queue_);
            cv_ready_.wait(lock, [&] { return !work_queue_.empty() || !running_; });
            // The event loop runs on the render thread, and coroutines resumed
            // to process the render tasks will also be run on the render
            // thread.
            work_available_.UnParkAll();
        }
    }

    uint32_t frames_in_flight_;
    std::atomic<bool> running_;
    mutable std::mutex mutex_work_queue_;
    mutable std::condition_variable cv_ready_;
    mutable std::queue<FrameRenderTask> work_queue_;
    oxygen::co::ParkingLot work_available_; // Renamed from work_ for clarity
};

} // namespace

template <>
struct oxygen::co::EventLoopTraits<RenderTaskDispatcher> {
    static void Run(RenderTaskDispatcher& rtd) { rtd.EventLoop(); }
    static void Stop(RenderTaskDispatcher& rtd) { rtd.Stop(); }
    static auto IsRunning(RenderTaskDispatcher& rtd) -> bool { return rtd.IsRunning(); }
    static auto EventLoopId(RenderTaskDispatcher& rtd) -> EventLoopID { return EventLoopID(&rtd); }
};

struct RenderThread::Impl {
    Impl(uint32_t frames_in_flight)
        : dispatcher_(frames_in_flight)
    {
    }
    oxygen::co::Event stop_;
    RenderTaskDispatcher dispatcher_;
    std::thread thread_;

    auto RenderLoopAsync() -> oxygen::co::Co<>
    {
        while (dispatcher_.IsRunning()) {
            // Wait for work to be available using the parking lot
            co_await dispatcher_.WorkAvailable();

            if (!dispatcher_.IsRunning()) {
                break;
            }

            auto render_frame = dispatcher_.GetNextTask();
            DCHECK_F(render_frame.operator bool());

            // gfx->BeginFrame();

            DLOG_F(INFO, "Processing frame render task");
            // co_await render_frame(*gfx);

            // gfx->SubmitFrame();
            // gfx->PresentFrame();
            // gfx->AdvanceFrame();
        }
    }
};

RenderThread::RenderThread(uint32_t frames_in_flight)
    : impl_(std::make_unique<RenderThread::Impl>(frames_in_flight))
{
    DCHECK_GT_F(frames_in_flight, 0UL, "The number of frames in flight must be > 0");
    Start();
}

RenderThread::~RenderThread()
{
    if (!impl_->stop_.Triggered()) {
        Stop();
    }
    if (impl_->thread_.joinable()) {
        impl_->thread_.join();
    }
}

void RenderThread::Start()
{
    DCHECK_F(!impl_->dispatcher_.IsRunning());

    impl_->thread_ = std::thread([this]() {
        loguru::set_thread_name("render");
        DLOG_F(INFO, "Render thread started");
        oxygen::co::Run(impl_->dispatcher_, [this]() -> oxygen::co::Co<> {
            // NOLINTNEXTLINE(*-capturing-lambda-coroutines, *-reference-coroutine-parameters)
            OXCO_WITH_NURSERY(n)
            {
                // Start the render loop coroutine, which will run on the render
                // thread.
                n.Start(&RenderThread::Impl::RenderLoopAsync, impl_.get());

                // Start a background task to handle when the render thread
                // should be stopped. By canceling the nursery, we trigger
                // cancellation of all its running coroutines, thus terminating
                // the execution of the render thread.
                n.Start([this, &n]() -> oxygen::co::Co<> {
                    co_await impl_->stop_;
                    n.Cancel();
                    DLOG_F(INFO, "Render thread stopped");
                });

                // Wait for all tasks to complete
                co_return oxygen::co::kJoin;
            };
        });
    });
}

void RenderThread::Submit(FrameRenderTask task)
{
    impl_->dispatcher_.Submit(std::move(task));
}

void RenderThread::Stop()
{
    impl_->stop_.Trigger();
}
