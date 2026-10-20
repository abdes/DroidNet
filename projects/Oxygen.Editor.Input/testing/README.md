# Oxygen.Editor.Input.Testing

The test kit of `Oxygen.Editor.Input`: what other modules' tests use in place of
the input facts, instead of mocking `IInputSources` in each test. Only test
projects reference it.

- `FakeInputSources` is an in-memory `IInputSources`. `Add` reads an input
  source's JSON with the same format reader as `InputFacts`, `Remove` drops it,
  and `SetProblems` replaces the problems. Each raises its event at once, on the
  caller's thread.

The Input tests run the same reads against `FakeInputSources` and `InputFacts`,
so the fake answers as the real service does.
