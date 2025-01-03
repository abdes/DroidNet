-- Example using the Oxygen.Platform.SDL module
project "Oxygen.InputSystem.Example"
    language "C++"
    kind "ConsoleApp"
    location (workspace_root .. "/vs2022/Examples/InputSystem")
    files {
        "main.cpp",
        "MainModule.cpp",
        "MainModule.h",
    }
    links {
        "Oxygen",
        "Oxygen.Loader",
        "Oxygen.Platform.SDL",
    }
