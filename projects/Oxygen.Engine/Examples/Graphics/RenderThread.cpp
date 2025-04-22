//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ParkingLot.h>
#include <Oxygen/OxCo/Run.h>

#include <RenderThread.h>

using oxygen::RenderThread;

namespace {

class Renderer {
public:
    Renderer(std::weak_ptr<oxygen::Graphics> graphics, int frame_lag = 2)
        : gfx_weak_(std::move(graphics))
        , frame_lag_(frame_lag)
        , running_(false)
    {
    }

    ~Renderer() { Stop(); }

    void Stop()
    {
        if (!running_) {
            return;
        }
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cv_.notify_all();
        }
    }

    auto IsRunning() const -> bool { return running_; }

    // Called from the game/main thread to submit a frame for rendering.
    void Submit(RenderThread::RenderTask task)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // Wait if the queue is full (frame lag)
        cv_.wait(lock, [&] { return frame_queue_.size() < static_cast<size_t>(frame_lag_) || !running_; });
        frame_queue_.emplace(std::move(task));
        cv_.notify_all();
    }

private:
    template <typename T, typename>
    friend struct oxygen::co::EventLoopTraits;
    void EventLoop()
    {
        running_ = true;
        while (running_) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return !frame_queue_.empty() || !running_; });
            work_available_.UnParkAll();
        }
    }

    friend class RenderThread;
    auto RenderLoopAsync() -> oxygen::co::Co<>
    {
        while (running_) {
            co_await work_available_.Park();
            DCHECK_F(!frame_queue_.empty());
            if (!running_) {
                break;
            }
            // Process the frame queue
            RenderThread::RenderTask task = std::move(frame_queue_.front());
            frame_queue_.pop();
            cv_.notify_all();
            DCHECK_F(task.operator bool());

            if (gfx_weak_.expired()) {
                LOG_F(WARNING, "Graphics object expired, skipping frame");
                continue;
            }

            auto gfx = gfx_weak_.lock();

            // gfx->BeginFrame();
            task(*gfx);
            // gfx->SubmitFrame();
            // gfx->PresentFrame();
            // gfx->AdvanceFrame();
        }
    }

    std::weak_ptr<oxygen::Graphics> gfx_weak_;
    int frame_lag_;
    std::atomic<bool> running_;
    std::thread thread_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    mutable std::queue<RenderThread::RenderTask> frame_queue_;
    oxygen::co::ParkingLot work_available_;
};

} // namespace

template <>
struct oxygen::co::EventLoopTraits<Renderer> {
    static void Run(Renderer& renderer) { renderer.EventLoop(); }
    static void Stop(Renderer& renderer) { renderer.Stop(); }
    static auto IsRunning(Renderer& renderer) -> bool { return renderer.IsRunning(); }
    static auto EventLoopId(Renderer& renderer) -> EventLoopID { return EventLoopID(&renderer); }
};

struct RenderThread::Impl {
    Impl(std::weak_ptr<oxygen::Graphics> graphics, int frame_lag)
        : renderer_(std::move(graphics), frame_lag)
    {
    }
    oxygen::co::Event stop_;
    Renderer renderer_;
    std::thread thread_;
};

RenderThread::RenderThread(std::weak_ptr<oxygen::Graphics> graphics, int frame_lag)
    : impl_(std::make_unique<Impl>(std::move(graphics), frame_lag))
{
    Start();
}

RenderThread::~RenderThread()
{
    if (!impl_->stop_.Triggered()) {
        Stop();
    }
    if (impl_->thread_.joinable())
        impl_->thread_.join();
}

void RenderThread::Start()
{
    if (impl_->renderer_.IsRunning()) {
        DLOG_F(WARNING, "Render thread is already running");
        return;
    }
    impl_->thread_ = std::thread([this]() {
        loguru::set_thread_name("render");
        DLOG_F(INFO, "Render thread started");
        oxygen::co::Run(impl_->renderer_, [this]() -> oxygen::co::Co<> {
            // NOLINTNEXTLINE(*-capturing-lambda-coroutines, *-reference-coroutine-parameters)
            OXCO_WITH_NURSERY(n)
            {
                n.Start(&Renderer::RenderLoopAsync, &impl_->renderer_);

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

void RenderThread::Submit(RenderTask task)
{
    impl_->renderer_.Submit(std::move(task));
}

void RenderThread::Stop()
{
    impl_->stop_.Trigger();
}
