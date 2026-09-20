# CMake presets

The engine's `CMakePresets.json` selects the platform preset file in this directory.
`WindowsPresets.json`, `LinuxPresets.json`, and `MacPresets.json` include
`BasePresets.json`, which provides shared configuration and build settings and
includes the generated `ConanPresets-Ninja.json` from the engine root.

These files use schema version 6 and require CMake 3.29 or newer. Their include
paths are relative to the preset files. The platform files remain separate for
IDE compatibility; update shared settings in `BasePresets.json` and keep the
platform-specific preset names aligned with the CLI's preset discovery.

Generate the Conan preset inputs before configuring a build tree. Use
`tools/generate-builds.ps1 -Help` from the engine directory for the initialization
command's arguments.
