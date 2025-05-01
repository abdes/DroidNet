//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string_view>
#include <thread>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Detail/RenderThread.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
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
        DLOG_F(INFO, "Stopping render task dispatcher");
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
    auto GetNextTask() -> FrameRenderTask
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
            lock.unlock();
            work_available_.UnParkAll();
        }
    }

    uint32_t frames_in_flight_;
    std::atomic<bool> running_ { false };
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
    Impl(
        uint32_t frames_in_flight,
        RenderThread::BeginFrameFn begin_frame_fn,
        RenderThread::EndFrameFn end_frame_fn)
        : dispatcher_(frames_in_flight)
        , begin_frame_fn_(std::move(begin_frame_fn))
        , end_frame_fn_(std::move(end_frame_fn))
    {
        DCHECK_LT_F(frames_in_flight, kFrameBufferCount,
            "The number of frames in flight must be < {}", kFrameBufferCount);
        DCHECK_F(begin_frame_fn_.operator bool());
        DCHECK_F(end_frame_fn_.operator bool());
    }

    auto RenderLoopAsync() -> oxygen::co::Co<>
    {
        DCHECK_F(!dispatcher_.IsRunning());
        while (true) {
            // Wait for work to be available using the parking lot
            co_await dispatcher_.WorkAvailable();

            // If work is available but the render thread is not running, then
            // it is shutting down and we need to stop the render loop.
            if (!dispatcher_.IsRunning()) {
                break;
            }

            LOG_SCOPE_F(1, fmt::format("Render frame ({})", debug_name_).c_str());
            auto render_frame = dispatcher_.GetNextTask();
            DCHECK_F(render_frame.operator bool());

            if (begin_frame_fn_) {
                auto& render_target = begin_frame_fn_();
            }

            // Execute the application rendering task, asynchronously. Such task
            // may be quite complex and may be composed of several coroutines
            // that need to complete together. Synchronization and completion
            // management are the responsibility of the application.
            {
                LOG_SCOPE_F(1, "Recording...");
                // TODO: pass the render target to the task
                co_await render_frame();
            }

            if (end_frame_fn_) {
                end_frame_fn_();
            }
        }
    }

    std::string_view debug_name_ {};
    oxygen::co::Event stop_;
    RenderTaskDispatcher dispatcher_;
    std::thread thread_;
    BeginFrameFn begin_frame_fn_;
    EndFrameFn end_frame_fn_;
};

RenderThread::RenderThread(
    uint32_t frames_in_flight,
    BeginFrameFn begin_frame_fn,
    EndFrameFn end_frame_fn)
    : impl_(std::make_unique<RenderThread::Impl>(frames_in_flight, std::move(begin_frame_fn), std::move(end_frame_fn)))
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
                    DLOG_F(1, "Cancel RenderThread nursery");
                    n.Cancel();
                });

                // Wait for all tasks to complete
                co_return oxygen::co::kJoin;
            };
        });
        impl_->dispatcher_.Stop();
        DLOG_F(INFO, "Render thread completed");
    });
}

void RenderThread::Submit(FrameRenderTask task)
{
    impl_->dispatcher_.Submit(std::move(task));
}

void RenderThread::Stop()
{
    if (!impl_->dispatcher_.IsRunning()) {
        return;
    }

    impl_->stop_.Trigger();
    if (impl_->thread_.joinable()) {
        impl_->thread_.join();
    }
}

void RenderThread::UpdateDependencies(const oxygen::Composition& composition)
{
    impl_->debug_name_ = composition.GetComponent<oxygen::ObjectMetaData>().GetName();
}
