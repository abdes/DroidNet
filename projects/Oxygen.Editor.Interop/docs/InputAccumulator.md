# InputAccumulator (host-side)

Purpose
- Host-agnostic component to coalesce high-frequency raw input into one logical update per engine frame.

Principles
- Per-viewport accumulators (mouse/scroll are summed; key/button events preserved in order).
- Thread-safe: host/UI thread(s) push; host frame-start drains on engine/dispatch thread.
- Focus-loss discards accumulated deltas but preserves ordered state transitions.

Public API
- `void PushMouseDelta(ViewId, Vector2f)`
- `void PushScroll(ViewId, float)`
- `void PushKeyEvent(ViewId, KeyEvent)`
- `void PushButtonEvent(ViewId, ButtonEvent)`
- `AccumulatedInput Drain(ViewId)`
- `void OnFocusLost(ViewId)`

Integration
- Create an `InputAccumulator` in the interop/host layer.
- In the host frame-start hook: call `Drain(view)` for the relevant `ViewId`s and forward the resulting events to the engine writer (`InputEvents::ForWrite()`).
- Use `InputAccumulatorAdapter` with an implementation of `IInputWriter` that wraps the engine writer.

Testing
- Unit tests should validate accumulation, ordering, focus-loss semantics, and multi-view isolation. Implement a direct-injection harness that spawns a writer thread to push events rapidly and a test thread to `Drain()` once.
