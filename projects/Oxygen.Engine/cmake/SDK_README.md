# Oxygen SDK

This SDK contains Oxygen, its public dependency headers and libraries, native
content tools, and the RenderScene showcase. No Conan installation or Oxygen
checkout is needed to consume it.

## Run the showcase

From this directory, run `RenderScene.cmd`. It locates the bundled executable and
content without changing PATH. See [RenderScene](share/oxygen/RenderScene/README.md)
for scene selection and [Content](share/oxygen/Content/README.md) for editing,
cooking, and packing the supplied samples.

## Build your application

Use CMake 4.2+, an x64 MSVC 19.50+ toolchain, and C++23. Match your application
configuration to the SDK configuration you received. For example, from an x64
developer shell with a Debug SDK:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="F:/path/to/OxygenSDK"
cmake --build build
```

A minimal CMake project linking the scene module is:

```cmake
cmake_minimum_required(VERSION 4.2)
project(MyGame LANGUAGES CXX)
find_package(Oxygen CONFIG REQUIRED COMPONENTS Scene)
add_executable(my-game main.cpp)
target_compile_features(my-game PRIVATE cxx_std_23)
target_link_libraries(my-game PRIVATE oxygen::scene)

# Stage the SDK's DLLs beside the app for execution from an ordinary shell.
file(GLOB sdk_dlls "${OXYGEN_RUNTIME_DIR}/*.dll")
if(sdk_dlls)
  add_custom_command(TARGET my-game POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      ${sdk_dlls} "$<TARGET_FILE_DIR:my-game>"
    COMMAND_EXPAND_LISTS VERBATIM)
endif()
```

Oxygen targets supply their public dependency usage requirements. Do not add
producer checkout paths or Conan cache paths to your application. SDK locations
are available as `OXYGEN_SDK_ROOT`, `OXYGEN_RUNTIME_DIR`, and `OXYGEN_DATA_DIR`.

## Layout and relocation

- `include`, `lib`, `bin`: Oxygen and dependency development/runtime files.
- `lib/cmake`: native CMake package definitions.
- `share/oxygen/schemas`: content authoring and tool schemas.
- `share/oxygen/Content`: curated source inputs, cooked scenes, PAK, and scripts.
- `share/oxygen/RenderScene`: showcase documentation and local settings.
- `share/oxygen/licenses`: dependency license notices.

Move or archive the complete SDK directory. RenderScene resolves its resources
from that tree; SDK-local paths in its settings follow the move. External assets
remain at the paths you selected. The showcase's settings and content outputs
currently require a writable SDK location.
