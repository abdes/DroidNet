# Oxygen SDK developer apps

These standalone console applications consume an installed Oxygen SDK. They have
no source-tree includes, Conan toolchain, or direct third-party target links.

- `scene-playground`: creates an Oxygen scene with a player and child camera,
  changes the player transform, and checks the camera world transform. Enter
  `a` or `d` to move and `q` to exit.
- `async-loading`: uses Oxygen OxCo coroutines and its Asio integration to run
  four timed stages. Asset work is simulated; scheduling, suspension, completion,
  and return from the event loop are real. Enter repeats; `q` exits.

From an x64 MSVC developer shell, with CMake 4.2+ and Ninja available:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="F:/path/to/Oxygen/Debug"
cmake --build build
ctest --test-dir build --output-on-failure
.\build\scene-playground.exe
.\build\async-loading.exe
```

Match the build configuration to the installed SDK. CMake stages the SDK DLLs
beside these executables, so they run in an ordinary shell without changing PATH.
`Run.ps1 -App scene-playground` or `Run.ps1 -App async-loading` explicitly uses a
system-only PATH for runtime verification. Both apps support `--verify` for
noninteractive checks; interactive mode stays open until the user quits.
