# Link an application against Oxygen SDK

This is a standalone CMake project. It links Oxygen.Core and uses the bundled fmt
and GLM headers through Oxygen's exported dependencies. It does not use Conan or
add the engine's source tree to the build.

Use CMake 4.2+, Ninja and an x64 MSVC 19.50+ C++23 toolchain. From an x64 developer
shell, with a **Debug SDK** extracted at `F:/OxygenSDK`:

```powershell
cmake -S . -B ./hello-build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=F:/OxygenSDK
cmake --build ./hello-build
.\hello-build\hello-oxygen.exe
```

Match `CMAKE_BUILD_TYPE` to the SDK you received. For a Release SDK, use Release.
The sample copies the SDK DLLs beside its executable, so running it does not
require changing PATH or keeping a developer shell open.

Expected output includes the Oxygen version and a GLM dot product of `14`.
You can copy this project into your own workspace and use the same
`find_package(Oxygen CONFIG REQUIRED)` / `target_link_libraries` pattern.
