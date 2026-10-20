# Oxygen.Editor.Scripting.Testing

The test kit of `Oxygen.Editor.Scripting`: what other modules' tests use in
place of the script facts and their ports, instead of mocking them in each test.
Only test projects reference it.

- `FakeScripts` is an in-memory `IScripts`. `Add` and `Remove` change the
  project's scripts and `SetLibrary` sets what a library read returns. Each
  change raises its events at once, on the caller's thread. It records library
  reads and refreshes.
- `FakeScriptAnalyzer` is an `IScriptAnalyzer` that answers a source text with
  the analysis a test sets for it, and can make analyses wait as the real one
  does until the engine SDK is verified.
- `FakeScriptLibrary` is an `IScriptLibrary` holding the library scripts a test
  adds, and the reads it makes fail.
