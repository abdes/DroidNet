-- SDL3 Platform Layer
project "Oxygen.Platform.SDL"
    kind "SharedLib"
    location (workspace_root .. "/vs2022/Oxygen/Platform-SDL")
    files {
        "**.h",
        "**.cpp",
    }
    removefiles { "test/**" }
    defines { "OXYGEN_SDL3_EXPORTS" }
    links {
        "Oxygen",
        "SDL3-static",
        "Imm32",
        "Version",
        "Setupapi",
        "Winmm",
        "imgui" .. link_lib_suffix
    }
