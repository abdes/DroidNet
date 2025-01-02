-- Example using the Oxygen.Platform.SDL module
project "Oxygen.Platform.SDL.Example"
    kind "ConsoleApp"
    location (workspace_root .. "/vs2022/Examples/Platform-SDL")
    files {
        "main.cpp",
    }
    links {
        "Oxygen",
        "Oxygen.Platform.SDL",
        "SDL3-static"
    }
