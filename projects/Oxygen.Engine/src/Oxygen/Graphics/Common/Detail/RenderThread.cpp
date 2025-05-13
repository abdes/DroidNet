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
#include <Oxygen/Base/Macros.h>
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
    explicit RenderTaskDispatcher(const uint32_t frames_in_flight)
        : frames_in_flight_(frames_in_flight)
    {
        DCHECK_GT_F(frames_in_flight_, 0UL, "The number of frames in flight must be > 0");
    }

    ~RenderTaskDispatcher() { Stop(); }

    OXYGEN_MAKE_NON_COPYABLE(RenderTaskDispatcher)
    OXYGEN_MAKE_NON_MOVABLE(RenderTaskDispatcher)

    void Stop()
    {
        if (!running_) {
            return;
        }
        DLOG_F(INFO, "Stopping render task dispatcher");
        running_ = false;
        {
            std::lock_guard lock(mutex_work_queue_);
            cv_ready_.notify_all();
        }
    }

    auto IsRunning() const -> bool { return running_; }

    // ReSharper disable once CppMemberFunctionMayBeConst
    void Submit(FrameRenderTask task)
    {
        std::unique_lock<std::mutex> lock(mutex_work_queue_);
        // Wait if the queue is full (frame lag)
        cv_ready_.wait(lock, [&] {
            return work_queue_.size() < static_cast<size_t>(frames_in_flight_) || !running_;
        });
        work_queue_.emplace(std::move(task));
        cv_ready_.notify_all();
    }

    // Wait for and retrieve the next render task
    auto GetNextTask() const -> FrameRenderTask
    {
        std::unique_lock lock(mutex_work_queue_);

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
            std::unique_lock lock(mutex_work_queue_);
            cv_ready_.wait(lock, [&] {
                return !work_queue_.empty() || !running_;
            });
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
    static auto IsRunning(const RenderTaskDispatcher& rtd) -> bool { return rtd.IsRunning(); }
    static auto EventLoopId(RenderTaskDispatcher& rtd) -> EventLoopID { return EventLoopID(&rtd); }
};

struct RenderThread::Impl {
    std::string_view debug_name {};
    co::Event stop;
    RenderTaskDispatcher dispatcher;
    std::thread thread;
    BeginFrameFn begin_frame_fn;
    EndFrameFn end_frame_fn;

    Impl(
        const uint32_t frames_in_flight,
        BeginFrameFn begin_frame,
        EndFrameFn end_frame)
        : dispatcher(frames_in_flight)
        , begin_frame_fn(std::move(begin_frame))
        , end_frame_fn(std::move(end_frame))
    {
        DCHECK_LT_F(frames_in_flight, kFrameBufferCount,
            "The number of frames in flight must be < {}", kFrameBufferCount);
        DCHECK_F(begin_frame_fn.operator bool());
        DCHECK_F(end_frame_fn.operator bool());
    }

    auto RenderLoopAsync() -> co::Co<>
    {
        DCHECK_F(!dispatcher.IsRunning());
        while (true) {
            // Wait for work to be available using the parking lot
            co_await dispatcher.WorkAvailable();

            // If work is available but the render thread is not running, then
            // it is shutting down, and we need to stop the render loop.
            if (!dispatcher.IsRunning()) {
                break;
            }

            LOG_SCOPE_F(1, "Render frame");
            DLOG_F(1, "Renderer: {}", debug_name);
            auto render_frame = dispatcher.GetNextTask();
            DCHECK_F(render_frame.operator bool());

            if (begin_frame_fn) {
                try {
                    begin_frame_fn();
                } catch (const std::exception& ex) {
                    LOG_F(ERROR, "Exception caught in BeginFrame(), and the frame will be dropped: {}", ex.what());
                    continue;
                }
            }

            // Execute the application rendering task, asynchronously. Such task
            // may be quite complex and may be composed of several coroutines
            // that need to complete together. Synchronization and completion
            // management are the responsibility of the application.
            {
                LOG_SCOPE_F(1, "Execute render task...");
                try {
                    co_await render_frame();
                } catch (const std::exception& ex) {
                    LOG_F(ERROR, "Exception caught in Render Frame Task, frame may be garbage: {}", ex.what());
                }
            }

            if (end_frame_fn) {
                try {
                    end_frame_fn();
                } catch (const std::exception& ex) {
                    LOG_F(ERROR, "Exception caught in EndFrame(), and the rendering task dispatcher will be stopped: {}", ex.what());
                    stop.Trigger();
                }
            }
        }
    }
};

RenderThread::RenderThread(
    uint32_t frames_in_flight,
    BeginFrameFn begin_frame,
    EndFrameFn end_frame)
    : impl_(std::make_unique<Impl>(
          frames_in_flight,
          std::move(begin_frame),
          std::move(end_frame)))
{
    DCHECK_GT_F(frames_in_flight, 0UL, "The number of frames in flight must be > 0");
    Start();
}

RenderThread::~RenderThread()
{
    if (!impl_->stop.Triggered()) {
        Stop();
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

void RenderThread::Start()
{
    impl_->thread = std::thread([this]() {
        loguru::set_thread_name("render");
        DLOG_F(INFO, "Render thread started");
        co::Run(impl_->dispatcher, [this]() -> co::Co<> {
            OXCO_WITH_NURSERY(n)
            {
                // Start the render loop coroutine, which will run on the render
                // thread.
                n.Start(&Impl::RenderLoopAsync, impl_.get());

                // Start a background task to handle when the render thread
                // should be stopped. By canceling the nursery, we trigger
                // cancellation of all its running coroutines, thus terminating
                // the execution of the render thread.
                n.Start([this, &n]() -> co::Co<> {
                    co_await impl_->stop;
                    DLOG_F(1, "Cancel RenderThread nursery");
                    n.Cancel();
                });

                // Wait for all tasks to complete
                co_return co::kJoin;
            };
        });
        impl_->dispatcher.Stop();
        DLOG_F(INFO, "Render thread completed");
    });
}

// ReSharper disable once CppMemberFunctionMayBeConst
void RenderThread::Submit(FrameRenderTask task)
{
    impl_->dispatcher.Submit(std::move(task));
}

// ReSharper disable once CppMemberFunctionMayBeConst
void RenderThread::Stop()
{
    if (!impl_->dispatcher.IsRunning()) {
        return;
    }

    impl_->stop.Trigger();
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

void RenderThread::UpdateDependencies(const Composition& composition)
{
    impl_->debug_name = composition.GetComponent<ObjectMetaData>().GetName();
}
