//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::AsyncEngine;
using oxygen::IAsyncEngine;
using oxygen::observer_ptr;
using oxygen::Platform;
using oxygen::PlatformConfig;
using oxygen::core::PhaseId;
using oxygen::engine::EngineModule;
using oxygen::engine::FrameContext;
using oxygen::engine::ModulePhaseMask;
using oxygen::engine::ModulePriority;
using SessionState = oxygen::IAsyncEngine::SessionState;
using std::chrono::nanoseconds;

using namespace std::chrono_literals;

//! What one probe saw in one frame.
struct FrameRecord {
  uint32_t fixed_runs { 0U };
  uint32_t gameplay_runs { 0U };
  nanoseconds game_delta { 0 };
  bool is_paused { false };
  float time_scale { 1.0F };
};

using EventLog = std::vector<std::string>;

//! Per-frame records of one probe; outlives the probe, which the engine
//! destroys at shutdown.
class ProbeRecords {
public:
  [[nodiscard]] auto Frame(const uint64_t frame) const -> FrameRecord
  {
    const auto it = frames_.find(frame);
    return it != frames_.end() ? it->second : FrameRecord {};
  }

  [[nodiscard]] auto At(const uint64_t frame) -> FrameRecord&
  {
    return frames_[frame];
  }

private:
  std::map<uint64_t, FrameRecord> frames_;
};

//! Records phases, timing and session hooks, and runs a per-frame action.
class ProbeModule final : public EngineModule {
  OXYGEN_TYPED(ProbeModule)

public:
  using FrameAction = std::function<void(IAsyncEngine&, uint64_t frame)>;

  struct Options {
    std::string name;
    uint32_t priority { 500U };
    bool ticks_when_paused { false };
    bool refuse_session { false };
    //! Sets this scene on the frame, as a host does.
    std::shared_ptr<oxygen::scene::Scene> scene;
    //! Runs in kFrameStart and kGameplay respectively.
    FrameAction on_frame_start;
    FrameAction on_gameplay;
  };

  ProbeModule(Options options, std::shared_ptr<EventLog> log,
    std::shared_ptr<ProbeRecords> records)
    : options_(std::move(options))
    , log_(std::move(log))
    , records_(std::move(records))
  {
  }

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return options_.name;
  }
  [[nodiscard]] auto GetPriority() const noexcept -> ModulePriority override
  {
    return ModulePriority { options_.priority };
  }
  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> ModulePhaseMask override
  {
    return oxygen::engine::MakeModuleMask<PhaseId::kFrameStart,
      PhaseId::kFixedSimulation, PhaseId::kGameplay>();
  }
  [[nodiscard]] auto TicksWhenPaused() const noexcept -> bool override
  {
    return options_.ticks_when_paused;
  }

  auto OnAttached(observer_ptr<IAsyncEngine> engine) noexcept -> bool override
  {
    engine_ = engine;
    return true;
  }

  auto OnSessionBegin(observer_ptr<IAsyncEngine> /*engine*/) noexcept
    -> bool override
  {
    log_->push_back("begin:" + options_.name);
    return !options_.refuse_session;
  }

  auto OnSessionEnd(observer_ptr<IAsyncEngine> /*engine*/) noexcept
    -> void override
  {
    const bool has_scene = context_ != nullptr && context_->GetScene();
    log_->push_back("end:" + options_.name + (has_scene ? "" : ":no-scene"));
  }

  auto OnFrameStart(observer_ptr<FrameContext> context) -> void override
  {
    context_ = context;
    const auto frame = context->GetFrameSequenceNumber().get();
    log_->push_back("frame:" + std::to_string(frame) + ":" + options_.name);
    auto& record = records_->At(frame);
    const auto& timing = context->GetModuleTimingData();
    record.is_paused = timing.is_paused;
    record.time_scale = timing.time_scale;
    if (options_.scene) {
      context->SetScene(observer_ptr { options_.scene.get() });
    }
    if (options_.on_frame_start) {
      options_.on_frame_start(*engine_, frame);
    }
  }

  auto OnFixedSimulation(observer_ptr<FrameContext> context)
    -> oxygen::co::Co<> override
  {
    ++records_->At(context->GetFrameSequenceNumber().get()).fixed_runs;
    co_return;
  }

  auto OnGameplay(observer_ptr<FrameContext> context)
    -> oxygen::co::Co<> override
  {
    const auto frame = context->GetFrameSequenceNumber().get();
    auto& record = records_->At(frame);
    ++record.gameplay_runs;
    record.game_delta = context->GetGameDeltaTime().get();
    if (options_.on_gameplay) {
      options_.on_gameplay(*engine_, frame);
    }
    co_return;
  }

private:
  Options options_;
  std::shared_ptr<EventLog> log_;
  std::shared_ptr<ProbeRecords> records_;
  observer_ptr<IAsyncEngine> engine_;
  observer_ptr<FrameContext> context_;
};

struct LoopContext {
  std::shared_ptr<Platform> platform;
  std::atomic_bool running { false };
};

} // namespace

template <> struct oxygen::co::EventLoopTraits<LoopContext> {
  static auto Run(LoopContext& ctx) -> void
  {
    while (ctx.running.load()) {
      ctx.platform->Async().PollOne();
    }
  }
  static auto Stop(LoopContext& ctx) -> void { ctx.running.store(false); }
  static auto IsRunning(const LoopContext& ctx) -> bool
  {
    return ctx.running.load();
  }
  static auto EventLoopId(LoopContext& ctx) -> EventLoopID
  {
    return EventLoopID(&ctx);
  }
};

namespace {

//! Runs a headless engine with the input system and the given probes for a
//! fixed number of frames.
class RunStateTest : public testing::Test {
protected:
  void SetUp() override
  {
    loop_.platform
      = std::make_shared<Platform>(PlatformConfig { .headless = true });
    oxygen::GraphicsConfig config {};
    config.headless = true;
    gfx_ = oxygen::GraphicsBackendLoader::GetInstanceRelaxed().LoadBackend(
      oxygen::graphics::BackendType::kHeadless, config, {});
    ASSERT_FALSE(gfx_.expired());
  }

  void TearDown() override
  {
    engine_.reset();
    if (const auto gfx = gfx_.lock()) {
      gfx->Stop();
    }
    oxygen::GraphicsBackendLoader::GetInstanceRelaxed().UnloadBackend();
    loop_.platform->Stop();
    loop_.platform.reset();
  }

  //! Adds a probe; its records stay readable after the engine stops.
  auto AddProbe(ProbeModule::Options options) -> ProbeRecords&
  {
    auto records = std::make_shared<ProbeRecords>();
    pending_.push_back(
      std::make_unique<ProbeModule>(std::move(options), log_, records));
    records_.push_back(records);
    return *records;
  }

  auto Run(const uint32_t frames) -> void
  {
    engine_ = std::make_shared<AsyncEngine>(loop_.platform, gfx_,
      oxygen::EngineConfig { .target_fps = 0U, .frame_count = frames });
    const auto exit_code = oxygen::co::Run(loop_, Main());
    ASSERT_EQ(exit_code, EXIT_SUCCESS);
  }

  [[nodiscard]] auto Engine() const -> AsyncEngine& { return *engine_; }
  [[nodiscard]] auto Log() const -> const EventLog& { return *log_; }

  //! Index of `entry` in the event log, or the log size when absent.
  [[nodiscard]] auto IndexOf(const std::string_view entry) const -> std::size_t
  {
    for (std::size_t i = 0; i < log_->size(); ++i) {
      if ((*log_)[i] == entry) {
        return i;
      }
    }
    return log_->size();
  }

  [[nodiscard]] static auto FixedStep() -> nanoseconds
  {
    return oxygen::TimingConfig {}.fixed_delta.get();
  }

private:
  auto Main() -> oxygen::co::Co<int>
  {
    OXCO_WITH_NURSERY(n)
    {
      loop_.running.store(true);
      co_await n.Start(&Platform::ActivateAsync, std::ref(*loop_.platform));
      loop_.platform->Run();
      const auto gfx = gfx_.lock();
      co_await n.Start(&oxygen::Graphics::ActivateAsync, std::ref(*gfx));
      gfx->Run();
      co_await n.Start(&AsyncEngine::ActivateAsync, std::ref(*engine_));
      engine_->Run();

      EXPECT_TRUE(
        engine_->RegisterModule(std::make_unique<oxygen::engine::InputSystem>(
          loop_.platform->Input().ForRead())));
      for (auto& probe : pending_) {
        EXPECT_TRUE(engine_->RegisterModule(std::move(probe)));
      }
      pending_.clear();

      co_await engine_->Completed();
      co_return oxygen::co::kCancel;
    };
    co_return EXIT_SUCCESS;
  }

  LoopContext loop_;
  std::weak_ptr<oxygen::Graphics> gfx_;
  std::shared_ptr<AsyncEngine> engine_;
  std::shared_ptr<EventLog> log_ { std::make_shared<EventLog>() };
  std::vector<std::unique_ptr<EngineModule>> pending_;
  std::vector<std::shared_ptr<ProbeRecords>> records_;
};

//! While paused, kFixedSimulation and kGameplay run only for modules that
//! tick when paused, which see a zero game delta. A request made mid-frame
//! applies at the next frame start.
NOLINT_TEST_F(RunStateTest, PauseStopsSimulationExceptTickingModules)
{
  // Arrange
  auto& regular = AddProbe({
    .name = "regular",
    .on_gameplay = [](IAsyncEngine& engine, const uint64_t frame) -> void {
      if (frame == 2U) {
        engine.Pause();
      }
    },
  });
  auto& ticking = AddProbe({ .name = "ticking", .ticks_when_paused = true });

  // Act
  Run(6U);

  // Assert
  EXPECT_EQ(regular.Frame(2U).gameplay_runs, 1U);
  EXPECT_FALSE(regular.Frame(2U).is_paused);
  for (uint64_t frame = 3U; frame <= 6U; ++frame) {
    SCOPED_TRACE(frame);
    EXPECT_TRUE(regular.Frame(frame).is_paused);
    EXPECT_EQ(regular.Frame(frame).gameplay_runs, 0U);
    EXPECT_EQ(regular.Frame(frame).fixed_runs, 0U);
    EXPECT_EQ(ticking.Frame(frame).gameplay_runs, 1U);
    EXPECT_EQ(ticking.Frame(frame).fixed_runs, 0U);
    EXPECT_EQ(ticking.Frame(frame).game_delta, nanoseconds { 0 });
  }
  EXPECT_TRUE(Engine().IsPaused());
}

//! Step(1) from paused runs kFixedSimulation once and kGameplay once with
//! the fixed timestep, however long the frame took, then stays paused.
NOLINT_TEST_F(RunStateTest, StepRunsOneFixedStepThenStaysPaused)
{
  // Arrange
  auto& probe = AddProbe({
    .name = "probe",
    .on_frame_start = [](IAsyncEngine& engine, const uint64_t frame) -> void {
      if (frame == 1U) {
        engine.Pause();
      }
      if (frame == 4U) {
        // A long wait must not change the stepped delta.
        std::this_thread::sleep_for(50ms);
        engine.Step(1U);
      }
    },
  });

  // Act
  Run(8U);

  // Assert
  EXPECT_EQ(probe.Frame(4U).gameplay_runs, 0U);
  const auto stepped = probe.Frame(5U);
  EXPECT_FALSE(stepped.is_paused);
  EXPECT_EQ(stepped.fixed_runs, 1U);
  EXPECT_EQ(stepped.gameplay_runs, 1U);
  EXPECT_EQ(stepped.game_delta, FixedStep());
  for (uint64_t frame = 6U; frame <= 8U; ++frame) {
    SCOPED_TRACE(frame);
    EXPECT_TRUE(probe.Frame(frame).is_paused);
    EXPECT_EQ(probe.Frame(frame).gameplay_runs, 0U);
    EXPECT_EQ(probe.Frame(frame).fixed_runs, 0U);
  }
  EXPECT_TRUE(Engine().IsPaused());
}

//! Step while running pauses first, then simulates the requested frames.
NOLINT_TEST_F(RunStateTest, StepFromRunningPausesFirst)
{
  // Arrange
  auto& probe = AddProbe({
    .name = "probe",
    .on_gameplay = [](IAsyncEngine& engine, const uint64_t frame) -> void {
      if (frame == 2U) {
        engine.Step(2U);
      }
    },
  });

  // Act
  Run(6U);

  // Assert
  for (uint64_t frame = 3U; frame <= 4U; ++frame) {
    SCOPED_TRACE(frame);
    EXPECT_EQ(probe.Frame(frame).fixed_runs, 1U);
    EXPECT_EQ(probe.Frame(frame).game_delta, FixedStep());
  }
  EXPECT_TRUE(probe.Frame(5U).is_paused);
  EXPECT_EQ(probe.Frame(5U).gameplay_runs, 0U);
  EXPECT_TRUE(Engine().IsPaused());
}

//! Resume cancels frame steps still pending: a later pause does not step.
NOLINT_TEST_F(RunStateTest, ResumeCancelsPendingSteps)
{
  // Arrange
  auto& probe = AddProbe({
    .name = "probe",
    .on_frame_start = [](IAsyncEngine& engine, const uint64_t frame) -> void {
      if (frame == 1U) {
        engine.Pause();
      }
      if (frame == 3U) {
        engine.Step(10U);
        engine.Resume();
      }
      if (frame == 5U) {
        engine.Pause();
      }
    },
  });

  // Act
  Run(9U);

  // Assert
  EXPECT_FALSE(probe.Frame(4U).is_paused);
  EXPECT_EQ(probe.Frame(4U).gameplay_runs, 1U);
  for (uint64_t frame = 6U; frame <= 9U; ++frame) {
    SCOPED_TRACE(frame);
    EXPECT_TRUE(probe.Frame(frame).is_paused);
    EXPECT_EQ(probe.Frame(frame).fixed_runs, 0U);
    EXPECT_EQ(probe.Frame(frame).gameplay_runs, 0U);
  }
}

//! The frame's time scale follows the simulation clock.
NOLINT_TEST_F(RunStateTest, TimeScaleFollowsClock)
{
  // Arrange
  auto& probe = AddProbe({
    .name = "probe",
    .on_frame_start
    = [this](IAsyncEngine& /*engine*/, const uint64_t frame) -> void {
      if (frame == 1U) {
        Engine().GetSimulationClock().SetTimeScale(0.5);
      }
    },
  });

  // Act
  Run(3U);

  // Assert
  EXPECT_EQ(probe.Frame(1U).time_scale, 1.0F);
  EXPECT_EQ(probe.Frame(2U).time_scale, 0.5F);
}

//! Session hooks run in priority order at begin and in reverse at end; the
//! end hooks run before any module's OnFrameStart and while the frame's
//! scene is still set.
NOLINT_TEST_F(RunStateTest, SessionHooksRunInOrderBeforeFrameStart)
{
  // Arrange
  auto scene = std::make_shared<oxygen::scene::Scene>("session-scene", 8U);
  auto& host = AddProbe({
    .name = "host",
    .priority = 100U,
    .scene = scene,
    .on_frame_start = [](IAsyncEngine& engine, const uint64_t frame) -> void {
      if (frame == 2U) {
        EXPECT_TRUE(engine.BeginSession());
        EXPECT_FALSE(engine.BeginSession());
      }
      if (frame == 5U) {
        engine.EndSession();
      }
    },
  });
  AddProbe({ .name = "first", .priority = 200U });
  AddProbe({ .name = "second", .priority = 300U });
  auto states = std::make_shared<std::map<uint64_t, SessionState>>();
  AddProbe({
    .name = "observer",
    .priority = 400U,
    .on_frame_start = [states](IAsyncEngine& engine, const uint64_t frame)
      -> void { (*states)[frame] = engine.GetSessionState(); },
  });
  (void)host;

  // Act
  Run(7U);

  // Assert
  EXPECT_EQ((*states)[2U], SessionState::kNone);
  EXPECT_EQ((*states)[3U], SessionState::kRunning);
  EXPECT_EQ((*states)[5U], SessionState::kRunning);
  EXPECT_EQ((*states)[6U], SessionState::kNone);

  const auto begin_host = IndexOf("begin:host");
  EXPECT_LT(begin_host, IndexOf("begin:first"));
  EXPECT_LT(IndexOf("begin:first"), IndexOf("begin:second"));
  EXPECT_LT(IndexOf("frame:2:host"), begin_host);
  EXPECT_LT(begin_host, IndexOf("frame:3:host"));

  const auto end_second = IndexOf("end:second");
  EXPECT_LT(end_second, IndexOf("end:first"));
  EXPECT_LT(IndexOf("end:first"), IndexOf("end:host"));
  EXPECT_LT(IndexOf("frame:5:observer"), end_second);
  EXPECT_LT(IndexOf("end:observer"), IndexOf("frame:6:host"));
}

//! A module that refuses the session leaves no session, the modules that
//! began get OnSessionEnd, and a later session can be requested.
NOLINT_TEST_F(RunStateTest, RefusedSessionRollsBack)
{
  // Arrange
  auto states = std::make_shared<std::map<uint64_t, SessionState>>();
  auto accepted = std::make_shared<std::map<uint64_t, bool>>();
  AddProbe({
    .name = "accepts",
    .priority = 100U,
    .on_frame_start
    = [states, accepted](IAsyncEngine& engine, const uint64_t frame) -> void {
      (*states)[frame] = engine.GetSessionState();
      if (frame == 2U || frame == 4U) {
        (*accepted)[frame] = engine.BeginSession();
      }
    },
  });
  AddProbe({ .name = "refuses", .priority = 200U, .refuse_session = true });

  // Act
  Run(5U);

  // Assert
  EXPECT_TRUE((*accepted)[2U]);
  EXPECT_EQ((*states)[3U], SessionState::kNone);
  EXPECT_TRUE((*accepted)[4U]);
  EXPECT_LT(IndexOf("begin:accepts"), IndexOf("begin:refuses"));
  EXPECT_LT(IndexOf("begin:refuses"), IndexOf("end:accepts"));
  EXPECT_EQ(IndexOf("end:refuses"), Log().size());
}

//! A session starts unpaused and paused sessions report kPaused; ending a
//! session clears the pause.
NOLINT_TEST_F(RunStateTest, SessionResetsPause)
{
  // Arrange
  auto states = std::make_shared<std::map<uint64_t, SessionState>>();
  auto& probe = AddProbe({
    .name = "probe",
    .on_frame_start
    = [states](IAsyncEngine& engine, const uint64_t frame) -> void {
      (*states)[frame] = engine.GetSessionState();
      if (frame == 1U) {
        engine.Pause();
      }
      if (frame == 3U) {
        EXPECT_TRUE(engine.BeginSession());
      }
      if (frame == 5U) {
        engine.Pause();
      }
      if (frame == 7U) {
        engine.EndSession();
      }
    },
  });

  // Act
  Run(9U);

  // Assert
  EXPECT_TRUE(probe.Frame(3U).is_paused);
  EXPECT_FALSE(probe.Frame(4U).is_paused);
  EXPECT_EQ((*states)[4U], SessionState::kRunning);
  EXPECT_EQ((*states)[6U], SessionState::kPaused);
  EXPECT_EQ((*states)[8U], SessionState::kNone);
  EXPECT_FALSE(probe.Frame(8U).is_paused);
  EXPECT_FALSE(Engine().IsPaused());
}

//! The console commands are the same requests.
NOLINT_TEST_F(RunStateTest, ConsoleCommandsPauseAndStep)
{
  // Arrange
  auto& probe = AddProbe({
    .name = "probe",
    .on_frame_start
    = [this](IAsyncEngine& /*engine*/, const uint64_t frame) -> void {
      auto& console = Engine().GetConsole();
      if (frame == 1U) {
        EXPECT_EQ(console.Execute("ngin.pause").status,
          oxygen::console::ExecutionStatus::kOk);
      }
      if (frame == 3U) {
        EXPECT_EQ(console.Execute("ngin.step 0").status,
          oxygen::console::ExecutionStatus::kInvalidArguments);
        EXPECT_EQ(console.Execute("ngin.step 2").status,
          oxygen::console::ExecutionStatus::kOk);
      }
    },
  });

  // Act
  Run(7U);

  // Assert
  EXPECT_TRUE(probe.Frame(2U).is_paused);
  EXPECT_EQ(probe.Frame(4U).fixed_runs, 1U);
  EXPECT_EQ(probe.Frame(5U).fixed_runs, 1U);
  EXPECT_TRUE(probe.Frame(6U).is_paused);
  EXPECT_EQ(probe.Frame(6U).fixed_runs, 0U);
}

} // namespace
