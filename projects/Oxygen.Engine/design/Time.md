
# Time Management System Design

**Document Version:** 1.0
**Target Implementation:** Oxygen Engine v1.0
**Status:** Approved for Implementation

## Executive Summary

This document defines a comprehensive, production-grade time management system
for the Oxygen Engine. The design emphasizes type safety, deterministic
simulation, performance, and clear separation of timing domains while providing
seamless migration from the current AsyncEngine implementation.

### Core Principles

1. **Domain Separation**: Each timing domain (Physical, Simulation,
   Presentation, Network, Timeline, Audit) has distinct types and
   responsibilities
2. **Type Safety**: Compile-time prevention of cross-domain timing errors
   through strong typing
3. **Deterministic Simulation**: Tick-based simulation timing for replay,
   networking, and debugging
4. **Performance First**: Zero-cost abstractions with nanosecond precision where
   needed
5. **Migration Friendly**: Clear upgrade path from current AsyncEngine timing
   code

## Timing Domains Overview

| Domain | Pausable | Scalable | Deterministic | Primary Use Cases |
|--------|:--------:|:--------:|:-------------:|-------------------|
| **Physical** | No | No | No | Frame pacing, profiling, GPU sync, engine infrastructure |
| **Simulation** | Yes | Yes | Yes | Physics, AI, gameplay logic, state transitions |
| **Presentation** | Yes | Yes | No | Animation blending, UI effects, visual interpolation |
| **Network** | No | No | Optional | Multiplayer synchronization, lag compensation |
| **Timeline** | Yes | Yes | Yes | Cutscenes, editor timeline, sequenced events |
| **Audit** | No | No | No | Logging, analytics, file timestamps, debugging |

## Core Architecture

### Type System

The time management system uses strongly-typed wrappers around chrono types to
prevent cross-domain errors and accidental raw type usage at compile time:

```cpp
namespace oxygen::time {

// Domain tags for type safety
struct PhysicalTag {};
struct SimulationTag {};
struct PresentationTag {};
struct NetworkTag {};
struct TimelineTag {};
struct AuditTag {};

// Strongly-typed duration wrapper prevents accidental raw chrono usage
// clang-format off
using CanonicalDuration = oxygen::NamedType<
    std::chrono::nanoseconds,
    struct CanonicalDurationTag,
    oxygen::Comparable,
    oxygen::Arithmetic,
    oxygen::Hashable,
    oxygen::Printable,
    oxygen::ImplicitlyConvertibleTo<std::chrono::nanoseconds>
>;
// clang-format on

// Strongly-typed time point wrappers prevent domain mixing
template<typename DomainTag>
// clang-format off
using TimePoint = oxygen::NamedType<
    std::chrono::time_point<std::chrono::steady_clock, std::chrono::nanoseconds>,
    DomainTag,
    oxygen::Comparable,
    oxygen::Hashable,
    oxygen::Printable,
    oxygen::ImplicitlyConvertibleTo<std::chrono::time_point<std::chrono::steady_clock, std::chrono::nanoseconds>>
>;
// clang-format on

// Domain-specific time point aliases for clarity and safety
using PhysicalTime = TimePoint<PhysicalTag>;
using SimulationTime = TimePoint<SimulationTag>;
using PresentationTime = TimePoint<PresentationTag>;
using NetworkTime = TimePoint<NetworkTag>;
using TimelineTime = TimePoint<TimelineTag>;

// Audit time uses system clock (wall clock time)
// clang-format off
using AuditTime = oxygen::NamedType<
    std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>,
    AuditTag,
    oxygen::Comparable,
    oxygen::Hashable,
    oxygen::Printable,
    oxygen::ImplicitlyConvertibleTo<std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>>
>;
// clang-format on

} // namespace oxygen::time
```

### Type Safety and Conversion Patterns

The NamedType wrappers provide compile-time safety while maintaining ergonomic
usage:

```cpp
namespace oxygen::time {

// SAFE: Explicit construction from raw types (prevents accidents)
auto duration = CanonicalDuration{std::chrono::milliseconds(16)}; // Explicit constructor
auto physical = PhysicalTime{std::chrono::steady_clock::now()};   // Explicit constructor

// SAFE: Implicit conversion to raw types (for std API compatibility)
std::chrono::nanoseconds raw_ns = duration;           // Implicit - convenient
auto std_time_point = static_cast<std::chrono::steady_clock::time_point>(physical); // Implicit

// SAFE: Domain separation prevents mixing
PhysicalTime physical_time = GetPhysicalClock().Now();
SimulationTime sim_time = GetSimulationClock().Now();
// auto mixed = physical_time + sim_time;              // COMPILE ERROR!

// SAFE: Arithmetic within same domain
CanonicalDuration delta{std::chrono::microseconds(16667)};
PhysicalTime next_frame = physical_time + delta;       // Works - same domain + duration

} // namespace oxygen::time

// PREVENTED: These would be compile errors
// CanonicalDuration bad = std::chrono::milliseconds(16);     // Missing explicit construction
// PhysicalTime bad = std::chrono::steady_clock::now();       // Missing explicit construction
// auto bad = physical_time + simulation_time;                // Domain mixing
// std::chrono::nanoseconds bad = raw_duration + duration;    // Raw + typed mixing
```

### Clock Interfaces

Each domain provides a clock interface with domain-appropriate capabilities:

```cpp
namespace oxygen::time {

// Base clock interface
template<typename DomainTag>
class ClockInterface {
public:
    virtual TimePoint<DomainTag> Now() const noexcept = 0;
    virtual ~ClockInterface() = default;
};

// Physical clock - monotonic, never paused/scaled
class PhysicalClock final : public ClockInterface<PhysicalTag> {
public:
    PhysicalTime Now() const noexcept override;

    // Get time since application start
    CanonicalDuration Uptime() const noexcept;
};

// Audit clock - wall clock time for logging/analytics
class AuditClock final {
public:
    AuditTime Now() const noexcept;

    // Convert between monotonic and wall clock
    AuditTime ToWallClock(PhysicalTime physical) const noexcept;
    PhysicalTime FromWallClock(AuditTime wall) const noexcept;
};

} // namespace oxygen::time
```

### Non-Owning Pointer Contract

Classes use `oxygen::observer_ptr` instead of reference members to maintain
movability while preserving safety:

- **Constructor Contract**: All clocks accept references at construction for
  convenience and null-safety
- **Storage Contract**: References are immediately converted to `observer_ptr`
  and stored internally
- **Lifetime Contract**: The `observer_ptr` is guaranteed to be non-null
  (constructed from reference)
- **Movability**: Classes remain movable unlike with reference members
- **API Consistency**: Public accessors return references, hiding the
  `observer_ptr` implementation detail

## Simulation Clock Design

The simulation clock provides fixed timestep simulation for consistent physics
and gameplay:

```cpp
namespace oxygen::time {

//! Fixed timestep simulation clock for consistent physics and gameplay.
/*!
 SimulationClock provides the standard fixed timestep with accumulator pattern
 used by most games for deterministic simulation behavior. This is the clock
 used for physics, AI, and other gameplay systems that require consistent timing.

 ### Key Features
 - **Fixed Timestep**: Configurable fixed timestep with accumulator pattern
 - **Pause/Scale Control**: Independent time scaling and pause functionality
 - **Interpolation Support**: Provides alpha for smooth rendering
 - **Spiral Prevention**: Configurable maximum substeps to prevent death spiral

 @see DeterministicClock for tick-based deterministic simulation
*/
class SimulationClock final : public ClockInterface<SimulationTag> {
public:
    explicit SimulationClock(CanonicalDuration fixed_timestep = std::chrono::microseconds(16667));

    // Core time access
    SimulationTime Now() const noexcept override;
    CanonicalDuration DeltaTime() const noexcept;

    // Runtime controls (legitimate state changes)
    void SetPaused(bool paused) noexcept;
    bool IsPaused() const noexcept;
    void SetTimeScale(double scale) noexcept; // scale >= 0.0
    double GetTimeScale() const noexcept;

    // Configuration (immutable after construction)
    CanonicalDuration GetFixedTimestep() const noexcept;

    // Frame integration - called by engine every frame
    void Advance(CanonicalDuration physical_elapsed) noexcept;

    // Fixed timestep execution
    struct FixedStepResult {
        uint32_t steps_executed;
        double interpolation_alpha; // [0.0, 1.0] for rendering interpolation
        CanonicalDuration remaining_time;
    };
    FixedStepResult ExecuteFixedSteps(uint32_t max_steps = 10) noexcept;

private:
    SimulationTime current_time_{};
    CanonicalDuration accumulated_time_{};
    const CanonicalDuration fixed_timestep_; // Immutable after construction
    bool is_paused_{false};
    double time_scale_{1.0};
};

} // namespace oxygen::time
```

## Deterministic Clock Design

For replay systems, networked games, and testing scenarios that require exact
reproducibility:

```cpp
namespace oxygen::time {

//! Tick-based deterministic simulation clock for replay and networking.
/*!
 DeterministicClock provides exact tick-based simulation for use cases that
 require perfect reproducibility, such as replay systems, networked games
 with rollback, or testing scenarios.

 ### Key Features
 - **Tick-Based Timing**: Integer tick counters for exact reproducibility
 - **Serializable State**: Full state serialization for replay/save systems
 - **Manual Advancement**: Explicit tick advancement for frame-by-frame control
 - **No Floating Point**: Avoids accumulation errors in deterministic paths

 ### Usage Pattern
 DeterministicClock is typically used in specialized scenarios:
 - Replay recording/playback systems
 - Networked games with rollback netcode
 - Unit testing with exact reproducibility
 - Frame-by-frame debugging

 @see SimulationClock for standard fixed timestep simulation
*/
class DeterministicClock final : public ClockInterface<SimulationTag> {
public:
    explicit DeterministicClock(CanonicalDuration tick_duration, uint64_t initial_seed = 0);

    // Core time access (based on current tick)
    SimulationTime Now() const noexcept override;
    CanonicalDuration DeltaTime() const noexcept;

    // Tick control
    void AdvanceTicks(uint64_t tick_count) noexcept;
    void SetCurrentTick(uint64_t tick) noexcept;
    uint64_t GetCurrentTick() const noexcept;

    // Configuration (immutable after construction)
    CanonicalDuration GetTickDuration() const noexcept;
    uint64_t GetRandomSeed() const noexcept;

    // Runtime control (legitimate state change)
    void SetPaused(bool paused) noexcept;
    bool IsPaused() const noexcept;

    // Serialization for replay systems
    struct SerializedState {
        uint64_t current_tick;
        bool is_paused;
        // Note: tick_duration and seed are constructor parameters, not serialized
    };
    SerializedState Serialize() const noexcept;
    void Deserialize(const SerializedState& state) noexcept;

private:
    uint64_t current_tick_{0};
    const CanonicalDuration tick_duration_; // Immutable after construction
    const uint64_t random_seed_; // Immutable after construction
    bool is_paused_{false};
};

} // namespace oxygen::time
```

## Presentation Clock Design

Presentation timing handles smooth visual interpolation and UI animations:

```cpp
namespace oxygen::time {

class PresentationClock final : public ClockInterface<PresentationTag> {
public:
    explicit PresentationClock(const SimulationClock& sim_clock, double animation_scale = 1.0);

    // Current presentation time (interpolated)
    PresentationTime Now() const noexcept override;
    CanonicalDuration DeltaTime() const noexcept;

    // Interpolation control (set by engine during frame)
    void SetInterpolationAlpha(double alpha) noexcept; // [0.0, 1.0]
    double GetInterpolationAlpha() const noexcept;

    // Configuration (immutable after construction)
    double GetAnimationScale() const noexcept;

    // Smooth timing for UI animations (framerate-independent)
    CanonicalDuration GetSmoothDeltaTime() const noexcept;

private:
    oxygen::observer_ptr<const SimulationClock> simulation_clock_; // Never null (from reference)
    const double animation_scale_; // Immutable after construction
    double interpolation_alpha_{0.0};
    mutable CanonicalDuration cached_smooth_delta_{};
    mutable PhysicalTime last_smooth_update_{};
};

// Interpolation utilities
namespace presentation {
    // Interpolate between two simulation time points
    PresentationTime Interpolate(SimulationTime previous, SimulationTime current,
                                double alpha) noexcept;

    // Smooth animation timing helpers
    double EaseInOut(double t) noexcept;
    double EaseIn(double t) noexcept;
    double EaseOut(double t) noexcept;
}

} // namespace oxygen::time
```

## Network Clock Design

Network timing handles synchronization and lag compensation:

```cpp
namespace oxygen::time {

class NetworkClock final {
public:
    // Offset management
    void SetPeerOffset(CanonicalDuration offset, double confidence = 1.0) noexcept;
    CanonicalDuration GetPeerOffset() const noexcept;
    double GetOffsetConfidence() const noexcept;

    // Time conversion
    PhysicalTime RemoteToLocal(NetworkTime remote_time) const noexcept;
    NetworkTime LocalToRemote(PhysicalTime local_time) const noexcept;

    // Lag compensation
    void SetRoundTripTime(CanonicalDuration rtt) noexcept;
    CanonicalDuration GetRoundTripTime() const noexcept;

    // Prediction and rollback support
    SimulationTime PredictRemoteTime(CanonicalDuration prediction_window) const noexcept;

    // Smoothing controls
    void SetSmoothingFactor(double factor) noexcept; // [0.0, 1.0]
    double GetSmoothingFactor() const noexcept;

    // Clock sync events
    struct SyncEvent {
        PhysicalTime local_time;
        NetworkTime remote_time;
        CanonicalDuration round_trip_time;
        double confidence;
    };
    void ProcessSyncEvent(const SyncEvent& event) noexcept;

private:
    CanonicalDuration peer_offset_{};
    double offset_confidence_{0.0};
    CanonicalDuration round_trip_time_{};
    double smoothing_factor_{0.1};

    // Offset history for smoothing
    static constexpr size_t kOffsetHistorySize = 16;
    std::array<CanonicalDuration, kOffsetHistorySize> offset_history_{};
    size_t offset_history_index_{0};
};

} // namespace oxygen::time
```

## Timeline Clock Design

Timeline management for cutscenes, editor, and sequenced events:

```cpp
namespace oxygen::time {

class TimelineClock final : public ClockInterface<TimelineTag> {
public:
    //! Configuration for timeline construction.
    struct Config {
        CanonicalDuration duration{std::chrono::seconds(60)}; // Default 1 minute
        bool loop{false};
        double playback_speed{1.0};
        CanonicalDuration step_size{std::chrono::milliseconds(33)}; // ~30fps for editor stepping

        // Deterministic mode settings
        bool deterministic_mode{false};
        CanonicalDuration tick_duration{std::chrono::microseconds(16667)};
    };

    explicit TimelineClock(const Config& config = {});

    // Timeline control
    TimelineTime Now() const noexcept override;
    void SetTime(TimelineTime time) noexcept;
    void Seek(CanonicalDuration offset, bool emit_skipped_events = false) noexcept;

    // Playback control (legitimate runtime state changes)
    void Play() noexcept;
    void Pause() noexcept;
    void Stop() noexcept; // Reset to start
    bool IsPlaying() const noexcept;

    // Configuration access (immutable after construction)
    CanonicalDuration GetDuration() const noexcept;
    bool IsLooping() const noexcept;
    double GetPlaybackSpeed() const noexcept;
    CanonicalDuration GetStepSize() const noexcept;

    // Frame stepping for editor
    void StepForward() noexcept;
    void StepBackward() noexcept;

    // Event system integration
    using EventCallback = std::function<void(TimelineTime)>;
    void RegisterEventCallback(EventCallback callback) noexcept;

    // Deterministic mode support (if enabled at construction)
    void SeekToTick(uint64_t tick) noexcept;
    uint64_t GetCurrentTick() const noexcept;
    bool IsInDeterministicMode() const noexcept;

    // Integration with simulation clock
    void BindToSimulation(const SimulationClock& sim_clock) noexcept;
    void Advance(CanonicalDuration delta) noexcept;

private:
    TimelineTime current_time_{};
    const CanonicalDuration duration_;          // Immutable after construction
    const bool is_looping_;                     // Immutable after construction
    const double playback_speed_;               // Immutable after construction
    const CanonicalDuration step_size_;         // Immutable after construction

    bool is_playing_{false};
    std::vector<EventCallback> event_callbacks_;

    // Optional simulation clock binding (never null when bound)
    oxygen::observer_ptr<const SimulationClock> bound_simulation_clock_;

    // Deterministic support (immutable configuration)
    const bool deterministic_mode_;
    const CanonicalDuration tick_duration_;
    uint64_t current_tick_{0};
};

} // namespace oxygen::time
```

## Time Manager Component

The Time Manager serves as the central coordinator for all timing domains and
integrates into the AsyncEngine composition system:

```cpp
namespace oxygen::time {

//! TimeManager component for the AsyncEngine composition system.
/*!
 The TimeManager serves as the central coordinator for all timing domains in the
 Oxygen Engine. It manages Physical, Simulation, Presentation, Network, Timeline,
 and Audit clocks, providing frame integration and performance monitoring.

 This component integrates with the AsyncEngine composition system and is designed
 to be the single source of truth for all timing concerns within the engine.

 ### Configuration Philosophy

 Following RAII principles, all timing configuration is provided at construction
 time. Runtime controls are limited to legitimate state changes (pause/unpause,
 time scale adjustments) rather than fundamental timing parameters.

 ### Key Features
 - **Domain Coordination**: Manages all timing domains through specialized clocks
 - **Frame Integration**: Provides BeginFrame/EndFrame lifecycle integration
 - **Performance Monitoring**: Tracks frame timing metrics and health
 - **Component Integration**: Follows Oxygen composition patterns
 - **RAII Design**: Immutable configuration, mutable state only where appropriate

 @see Component, PhysicalClock, SimulationClock, PresentationClock
*/
class TimeManager final : public Component {
public:
    //! Configuration for TimeManager construction.
    struct Config {
        // Simulation timing
        CanonicalDuration fixed_timestep{std::chrono::microseconds(16667)}; // 60Hz
        double default_time_scale{1.0};
        bool start_paused{false};

        // Presentation timing
        double animation_scale{1.0};

        // Deterministic mode (optional)
        bool enable_deterministic_mode{false};
        CanonicalDuration deterministic_tick_duration{std::chrono::microseconds(16667)};
        uint64_t deterministic_seed{0};

        // Network timing
        double network_smoothing_factor{0.1};
    };

    //! Construct TimeManager with configuration and physical clock reference.
    explicit TimeManager(PhysicalClock& physical_clock, const Config& config = {});
    ~TimeManager() override = default;

    OXYGEN_MAKE_NON_COPYABLE(TimeManager);
    OXYGEN_DEFAULT_MOVABLE(TimeManager);

    // Clock access (immutable references)
    PhysicalClock& GetPhysicalClock() noexcept { return *physical_clock_; }
    const SimulationClock& GetSimulationClock() const noexcept { return simulation_clock_; }
    SimulationClock& GetSimulationClock() noexcept { return simulation_clock_; }
    const PresentationClock& GetPresentationClock() const noexcept { return presentation_clock_; }
    PresentationClock& GetPresentationClock() noexcept { return presentation_clock_; }
    const NetworkClock& GetNetworkClock() const noexcept { return network_clock_; }
    NetworkClock& GetNetworkClock() noexcept { return network_clock_; }
    const TimelineClock& GetTimelineClock() const noexcept { return timeline_clock_; }
    TimelineClock& GetTimelineClock() noexcept { return timeline_clock_; }
    const AuditClock& GetAuditClock() const noexcept { return audit_clock_; }
    AuditClock& GetAuditClock() noexcept { return audit_clock_; }

    // Deterministic clock access (configured at construction)
    bool IsInDeterministicMode() const noexcept { return deterministic_clock_ != nullptr; }
    const DeterministicClock* GetDeterministicClock() const noexcept { return deterministic_clock_.get(); }
    DeterministicClock* GetDeterministicClock() noexcept { return deterministic_clock_.get(); }

    // Frame integration - called by AsyncEngine
    void BeginFrame() noexcept;
    void EndFrame() noexcept;

    // Fixed timestep integration support
    struct FrameTimingData {
        CanonicalDuration physical_delta;
        CanonicalDuration simulation_delta;
        uint32_t fixed_steps_executed;
        double interpolation_alpha;
        double current_fps;
    };
    const FrameTimingData& GetFrameTimingData() const noexcept { return frame_data_; }

    // Performance monitoring
    struct PerformanceMetrics {
        CanonicalDuration average_frame_time;
        CanonicalDuration max_frame_time;
        double average_fps;
        uint64_t total_frames;
        CanonicalDuration simulation_time_debt;
    };
    PerformanceMetrics GetPerformanceMetrics() const noexcept;

private:
    oxygen::observer_ptr<PhysicalClock> physical_clock_; // Never null (from reference)
    SimulationClock simulation_clock_;          // Configured at construction
    PresentationClock presentation_clock_;      // Configured at construction
    NetworkClock network_clock_;                // Configured at construction
    TimelineClock timeline_clock_;              // Configured at construction
    AuditClock audit_clock_;

    // Optional deterministic clock (configured at construction)
    std::unique_ptr<DeterministicClock> deterministic_clock_;

    FrameTimingData frame_data_{};
    PhysicalTime last_frame_time_{};

    // Performance tracking
    static constexpr size_t kPerformanceHistorySize = 120; // 2 seconds at 60fps
    std::array<CanonicalDuration, kPerformanceHistorySize> frame_time_history_{};
    size_t performance_history_index_{0};
};

} // namespace oxygen::time

// Component registration macro usage
OXYGEN_COMPONENT(oxygen::time::TimeManager);
```

## Migration Strategy from AsyncEngine

### Current Implementation Analysis

The existing AsyncEngine implementation contains several timing mechanisms:

1. **Frame Timing**: Circular buffer smoothing with clamping
2. **Fixed Timestep**: Accumulator pattern with substep execution
3. **Frame Pacing**: Deadline-based pacing with safety margins
4. **Interpolation**: Alpha calculation for smooth rendering

### Migration Steps

#### Phase 1: Type System Introduction

Replace current timing code with strongly-typed equivalents:

```cpp
// Current: std::chrono::microseconds
// New:     CanonicalDuration (NamedType wrapper)

// Current: std::chrono::steady_clock::now()
// New:     TimeManager::GetPhysicalClock().Now()

// Current: module_timing.game_delta_time
// New:     TimeManager::GetSimulationClock().DeltaTime()

// SAFE: Explicit construction prevents accidents
// OLD: auto delta = std::chrono::microseconds(16667);        // Raw type
// NEW: auto delta = CanonicalDuration{std::chrono::microseconds(16667)}; // Type-safe wrapper
```

#### Phase 2: Clock Integration

Replace `UpdateFrameTiming()` method:

```cpp
// Before
auto AsyncEngine::UpdateFrameTiming(FrameContext& context) -> void {
    const auto current_time = std::chrono::steady_clock::now();
    const auto raw_delta = current_time - last_frame_time_;
    // ... existing implementation
}

// After
auto AsyncEngine::UpdateFrameTiming(FrameContext& context) -> void {
    time_manager_.BeginFrame();

    const auto& timing_data = time_manager_.GetFrameTimingData();

    // Set timing data in context
    engine::ModuleTimingData module_timing;
    module_timing.game_delta_time = timing_data.simulation_delta;
    module_timing.fixed_delta_time = time_manager_.GetSimulationClock().GetFixedTimestep();
    module_timing.interpolation_alpha = timing_data.interpolation_alpha;
    module_timing.current_fps = timing_data.current_fps;

    const auto tag = internal::EngineTagFactory::Get();
    context.SetModuleTimingData(module_timing, tag);
}
```

#### Phase 3: Fixed Timestep Migration

Replace `PhaseFixedSim()` implementation:

```cpp
// Before: Manual accumulator management
auto AsyncEngine::PhaseFixedSim(FrameContext& context) -> co::Co<> {
    // ... accumulator logic
    while (accumulated_fixed_time_ >= fixed_delta && steps < max_substeps) {
        // ... execute substep
        accumulated_fixed_time_ -= fixed_delta;
    }
}

// After: Use SimulationClock (clean, focused API)
auto AsyncEngine::PhaseFixedSim(FrameContext& context) -> co::Co<> {
    auto& sim_clock = time_manager_.GetSimulationClock();
    const auto step_result = sim_clock.ExecuteFixedSteps(max_substeps);

    for (uint32_t step = 0; step < step_result.steps_executed; ++step) {
        // Update context for this substep
        auto module_timing = context.GetModuleTimingData();
        module_timing.fixed_steps_this_frame = step + 1;
        context.SetModuleTimingData(module_timing, tag);

        // Execute module simulation
        co_await module_manager_->ExecutePhase(PhaseId::kFixedSimulation, context);
    }

    // Update final interpolation alpha
    auto module_timing = context.GetModuleTimingData();
    module_timing.interpolation_alpha = step_result.interpolation_alpha;
    context.SetModuleTimingData(module_timing, tag);
}
```

#### Phase 4: Frame Pacing Integration

Keep frame pacing in AsyncEngine but use Physical clock for timing:

```cpp
// Before: Manual deadline calculation using std::chrono
if (config_.target_fps > 0) {
    const auto period_ns = std::chrono::nanoseconds(
        1'000'000'000ull / static_cast<uint64_t>(config_.target_fps));
    // ... deadline management with std::chrono::steady_clock::now()
}

// After: Use TimeManager's PhysicalClock for frame pacing
if (config_.target_fps > 0) {
    auto& physical_clock = time_manager_.GetPhysicalClock();
    const auto period_ns = CanonicalDuration{std::chrono::nanoseconds(
        1'000'000'000ull / static_cast<uint64_t>(config_.target_fps))};

    // Update deadline using Physical clock - type-safe operations
    if (next_frame_deadline_.get().time_since_epoch().count() == 0) {
        next_frame_deadline_ = physical_clock.Now() + period_ns;
    } else {
        next_frame_deadline_ = next_frame_deadline_ + period_ns;
    }

    const auto now = physical_clock.Now();
    // ... rest of frame pacing logic using strongly-typed times
}

// Example TimeManager construction in AsyncEngine
auto AsyncEngine::InitializeTimeManager(const EngineConfig::TimingConfig& timing_config) -> void {
    // Physical clock is owned by engine
    physical_clock_ = std::make_unique<oxygen::time::PhysicalClock>();

    // TimeManager accepts reference but stores observer_ptr internally (never null)
    time_manager_ = std::make_unique<oxygen::time::TimeManager>(
        *physical_clock_,
        timing_config.ToTimeManagerConfig()
    );
}
```

### Configuration Migration

Update `EngineConfig::TimingConfig` to use constructor-based configuration:

```cpp
// Enhanced timing configuration following RAII principles
struct TimingConfig {
    // Fixed timestep settings (immutable after construction)
    oxygen::time::CanonicalDuration fixed_delta{std::chrono::microseconds(16667)}; // 60Hz
    oxygen::time::CanonicalDuration max_accumulator{std::chrono::milliseconds(100)};
    uint32_t max_substeps{4};

    // Frame pacing settings
    oxygen::time::CanonicalDuration pacing_safety_margin{std::chrono::microseconds(500)};
    bool enable_frame_pacing{true};

    // Simulation settings (immutable configuration + mutable state)
    double default_time_scale{1.0};  // Initial value, can be changed at runtime
    bool start_paused{false};        // Initial state, can be changed at runtime

    // Presentation settings (immutable after construction)
    double animation_scale{1.0};

    // Deterministic mode settings (immutable - requires reconstruction to change)
    bool enable_deterministic_mode{false};
    oxygen::time::CanonicalDuration deterministic_tick_duration{std::chrono::microseconds(16667)};
    uint64_t deterministic_seed{0};

    // Network timing (initial configuration)
    oxygen::time::CanonicalDuration network_tick_rate{std::chrono::milliseconds(50)}; // 20Hz
    double network_smoothing_factor{0.1};

    // Timeline settings (immutable after construction)
    oxygen::time::CanonicalDuration timeline_duration{std::chrono::seconds(60)};
    bool timeline_loop{false};
    double timeline_playback_speed{1.0};

    // Performance monitoring
    bool enable_performance_tracking{true};
    oxygen::time::CanonicalDuration performance_log_interval{std::chrono::seconds(5)};

    // Convert to TimeManager::Config
    oxygen::time::TimeManager::Config ToTimeManagerConfig() const noexcept {
        return oxygen::time::TimeManager::Config{
            .fixed_timestep = fixed_delta,
            .default_time_scale = default_time_scale,
            .start_paused = start_paused,
            .animation_scale = animation_scale,
            .enable_deterministic_mode = enable_deterministic_mode,
            .deterministic_tick_duration = deterministic_tick_duration,
            .deterministic_seed = deterministic_seed,
            .network_smoothing_factor = network_smoothing_factor,
        };
    }
};
```

## Domain Conversion Rules

### Safe Conversions

Type-safe conversions between domains:

```cpp
namespace oxygen::time::convert {
    // Physical <-> Audit (wall clock)
    AuditTime ToWallClock(PhysicalTime physical, const AuditClock& audit_clock) noexcept;
    PhysicalTime FromWallClock(AuditTime wall, const AuditClock& audit_clock) noexcept;

    // Simulation -> Presentation (interpolation)
    PresentationTime ToPresentation(SimulationTime sim_time,
                                   const PresentationClock& presentation_clock) noexcept;

    // Network conversions (with explicit uncertainty)
    struct NetworkConversionResult {
        PhysicalTime local_time;
        CanonicalDuration uncertainty;
        bool is_reliable;
    };
    NetworkConversionResult NetworkToLocal(NetworkTime network_time,
                                          const NetworkClock& network_clock) noexcept;

    // Timeline <-> Simulation (deterministic mode only)
    SimulationTime TimelineToSimulation(TimelineTime timeline_time) noexcept;
    TimelineTime SimulationToTimeline(SimulationTime sim_time) noexcept;
}

} // namespace oxygen::time::convert
```

### Prohibited Conversions

The following conversions are compile-time errors:

```cpp
// ERROR: Cannot mix timing domains
oxygen::time::PhysicalTime physical = GetPhysicalClock().Now();
oxygen::time::SimulationTime simulation = physical; // Compile error!

// ERROR: Cannot directly convert between arbitrary domains
oxygen::time::NetworkTime network_time = GetNetworkClock().LocalToRemote(physical);
oxygen::time::TimelineTime timeline = network_time; // Compile error!

// CORRECT: Use explicit conversion functions
auto timeline = oxygen::time::convert::NetworkToTimeline(network_time, network_clock, timeline_clock);
```

## Performance Considerations

### Zero-Cost Abstractions

The time management system is designed for zero runtime overhead:

```cpp
namespace oxygen::time {

// Strongly-typed time points compile to identical assembly as raw time_point
static_assert(sizeof(PhysicalTime) == sizeof(std::chrono::steady_clock::time_point));
static_assert(std::is_trivially_copyable_v<PhysicalTime>);

// Template-based conversions inline completely
template<typename SourceTag, typename TargetTag>
constexpr TimePoint<TargetTag> time_cast(TimePoint<SourceTag> source) noexcept {
    return TimePoint<TargetTag>{source.time_since_epoch()};
}

} // namespace oxygen::time
```

### Memory Layout

Optimized data layout for cache efficiency:

```cpp
// Time manager fits in single cache line (64 bytes)
class TimeManager {
    // Hot data first (accessed every frame)
    PhysicalTime last_frame_time_;          // 8 bytes
    CanonicalDuration accumulated_time_;    // 8 bytes
    double interpolation_alpha_;            // 8 bytes
    uint32_t frame_counter_;               // 4 bytes
    uint32_t fixed_steps_this_frame_;      // 4 bytes

    // Clock references (cold data)
    PhysicalClock& physical_clock_;         // 8 bytes pointer
    // ... other clocks
};
```

### Timing Precision

| Domain | Precision | Rationale |
|--------|-----------|-----------|
| Physical | Nanoseconds | GPU synchronization, profiling |
| Simulation | Microseconds (typical) | Balance precision vs. accumulation error |
| Presentation | Microseconds | Smooth 60Hz+ rendering |
| Network | Milliseconds (typical) | Network packet timing |
| Timeline | Microseconds | Precise cutscene timing |
| Audit | Milliseconds | Human-readable logs |

## Testing Strategy

### Unit Test Coverage

```cpp
// Domain separation tests
TEST(TimeSystem, DomainSeparation) {
    auto physical = PhysicalClock{}.Now();
    auto simulation = SimulationClock{}.Now();

    // Should not compile: static_assert(false);
    // auto mixed = physical + simulation;

    EXPECT_TRUE(true); // Test passes if it compiles
}

// Deterministic simulation tests
TEST(SimulationClock, DeterministicReplay) {
    SimulationClock clock{SimulationMode::Deterministic};
    clock.EnterDeterministicMode(std::chrono::microseconds(16667));

    // Record tick sequence
    std::vector<uint64_t> ticks;
    for (int i = 0; i < 100; ++i) {
        clock.AdvanceTicks(1);
        ticks.push_back(clock.GetCurrentTick());
    }

    // Reset and replay
    clock.EnterDeterministicMode(std::chrono::microseconds(16667));
    for (int i = 0; i < 100; ++i) {
        clock.AdvanceTicks(1);
        EXPECT_EQ(ticks[i], clock.GetCurrentTick());
    }
}

// Fixed timestep accuracy tests
TEST(SimulationClock, FixedTimestepAccuracy) {
    SimulationClock clock{CanonicalDuration{std::chrono::microseconds(16667)}}; // 60Hz configured at construction

    const auto epsilon = CanonicalDuration{std::chrono::nanoseconds(100)};

    // Test accumulator behavior - type-safe operations
    clock.Advance(CanonicalDuration{std::chrono::microseconds(50000)}); // 3 steps worth
    auto result = clock.ExecuteFixedSteps();

    EXPECT_EQ(result.steps_executed, 3u);
    EXPECT_NEAR(result.interpolation_alpha, 0.0, 0.01); // Minimal remainder

    // Verify timestep is immutable and type-safe
    EXPECT_EQ(clock.GetFixedTimestep(), CanonicalDuration{std::chrono::microseconds(16667)});

    // Verify type safety - these would be compile errors:
    // auto bad = clock.GetFixedTimestep() + std::chrono::milliseconds(1); // Raw + typed mixing
    // SimulationTime bad_time = std::chrono::steady_clock::now();         // Missing explicit construction
}

// Interpolation boundary tests
TEST(PresentationClock, InterpolationBoundaries) {
    SimulationClock sim_clock{};
    PresentationClock pres_clock{sim_clock};

    // Test alpha boundaries
    pres_clock.SetInterpolationAlpha(0.0);
    EXPECT_DOUBLE_EQ(pres_clock.GetInterpolationAlpha(), 0.0);

    pres_clock.SetInterpolationAlpha(1.0);
    EXPECT_DOUBLE_EQ(pres_clock.GetInterpolationAlpha(), 1.0);

    // Test clamping
    pres_clock.SetInterpolationAlpha(-0.5);
    EXPECT_GE(pres_clock.GetInterpolationAlpha(), 0.0);

    pres_clock.SetInterpolationAlpha(1.5);
    EXPECT_LE(pres_clock.GetInterpolationAlpha(), 1.0);
}
```

### Integration Tests

```cpp
// Engine integration test
TEST(AsyncEngineIntegration, TimeManagerIntegration) {
    auto platform = std::make_shared<TestPlatform>();
    auto graphics = std::make_shared<TestGraphics>();

    EngineConfig config;
    config.timing.fixed_delta = std::chrono::microseconds(16667);
    config.timing.enable_deterministic_mode = false;
    config.timing.animation_scale = 1.0;

    AsyncEngine engine{platform, graphics, config};

    // Run several frames and verify timing consistency
    for (int frame = 0; frame < 60; ++frame) {
        engine.Step(); // Single frame execution

        const auto& timing = engine.GetTimeManager().GetFrameTimingData();
        EXPECT_GT(timing.physical_delta.count(), 0);
        EXPECT_LE(timing.fixed_steps_executed, 4u); // Max substeps
        EXPECT_GE(timing.interpolation_alpha, 0.0);
        EXPECT_LE(timing.interpolation_alpha, 1.0);
    }

    // Verify configuration was applied correctly
    const auto& sim_clock = engine.GetTimeManager().GetSimulationClock();
    EXPECT_EQ(sim_clock.GetFixedTimestep(), std::chrono::microseconds(16667));
    EXPECT_FALSE(engine.GetTimeManager().IsInDeterministicMode());
}
```

## File Organization

### Header Structure

```text
src/Oxygen/Core/
├── Time.h                // single include header for all Time
src/Oxygen/Core/Time/
├── Types.h                // Core types and aliases
├── PhysicalClock.h       // Physical timing implementation
├── SimulationClock.h     // Standard fixed timestep simulation
├── DeterministicClock.h  // Tick-based deterministic simulation (for replay/networking)
├── PresentationClock.h   // Presentation and interpolation utilities
├── NetworkClock.h        // Network synchronization
├── TimelineClock.h       // Timeline and sequencing
├── AuditClock.h         // Wall clock and logging support
├── TimeManager.h        // Central coordinator component
├── Conversion.h         // Domain conversion utilities
```

### Implementation Structure

```text
src/Oxygen/Core/Time/
├── PhysicalClock.cpp
├── SimulationClock.cpp
├── DeterministicClock.cpp
├── PresentationClock.cpp
├── NetworkClock.cpp
├── TimelineClock.cpp
├── AuditClock.cpp
├── TimeManager.cpp
├── Conversion.cpp
```

## Implementation Checklist

### Phase 1: Core Infrastructure

- [X] Create `src/Oxygen/Core/Time/Types.h` with domain tags and type aliases in
  `oxygen::time` namespace
- [X] Implement `PhysicalClock` with high-precision timing
- [X] Implement `AuditClock` with wall clock conversions
- [X] Implement `SimulationClock` with fixed timestep and accumulator pattern
- [X] Create comprehensive unit tests

### Phase 2: Presentation & Network

- [X] Implement `PresentationClock` with smooth interpolation
- [X] Add animation timing utilities and easing functions
- [X] Implement `NetworkClock` with offset management and smoothing
- [X] Create presentation and network timing tests

### Phase 3: DeterministicClock, Timeline & Integration

- [ ] Implement `DeterministicClock` as separate class for tick-based deterministic simulation
- [ ] Add serialization support to `DeterministicClock` for replay systems
- [ ] Implement `TimelineClock` with seek, loop, and event support

### Phase 4: Integration

- [X] Create `TimeManager` in src/Oxygen/Engine/TimeManager.h and .cpp as a `oxygen::Component`
- [X] Add domain conversion utilities with safety checks in `oxygen::time::convert` namespace
- [X] Implement comprehensive integration tests

### Phase 5: Engine Migration

- [X] Update `AsyncEngine` to integrate `TimeManager` component into its composition
- [X] Migrate `UpdateFrameTiming()` to use new time system
- [X] Replace fixed timestep logic in `PhaseFixedSim()` to use `SimulationClock`
- [X] Update frame pacing to use `PhysicalClock` while keeping logic in `AsyncEngine`
- [X] Update all engine configuration structures to use `oxygen::time` types

## Industry Best Practices Compliance

### Deterministic Simulation

- Tick-based simulation with integer counters
- No floating-point accumulation in deterministic paths
- Serializable state for replay and debugging
- Network synchronization support

### Performance Optimization

- Zero-cost abstractions with compile-time type safety
- Cache-friendly data layout
- Minimal memory allocations
- Optimized for 60Hz+ frame rates

### Robustness

- "Spiral of death" prevention with accumulator clamping
- Graceful handling of frame rate spikes
- Network timing resilience with confidence tracking
- Comprehensive error handling and validation

### Developer Experience

- Clear domain separation prevents timing bugs
- Rich debugging information and performance metrics
- Easy migration path from existing code
- Comprehensive test coverage and documentation

This time management system provides a solid foundation for professional game
engine development, ensuring accurate timing, deterministic simulation, and
smooth presentation across all engine subsystems.
