-- Example using the Oxygen.Platform.SDL module
project "Oxygen.InputSystem.Example"
    kind "ConsoleApp"
    location (workspace_root .. "/vs2022/Examples/InputSystem")
    files {
        "main.cpp",
        "MainModule.cpp",
        "MainModule.h",
    }
    links {
        "Oxygen",
        "Oxygen.Platform.SDL",
    }
