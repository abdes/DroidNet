-- Example project using the Oxygen.Renderer.Direct3D12 module
project "Oxygen.Graphics.Direct3D12.Example"
    language "C++"
    kind "ConsoleApp"
    location (workspace_root .. "/vs2022/Examples/D3D12-Renderer")
    files {
        "main.cpp",
        "MainModule.h",
        "MainModule.cpp",
    }
    links {
        "Oxygen",
        "Oxygen.Platform.SDL",
        "Oxygen.Graphics.Direct3D12",
        "Oxygen.Loader",
        "imgui" .. link_lib_suffix,
    }
