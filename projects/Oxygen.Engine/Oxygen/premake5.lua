-- Core Engine
project "Oxygen"
    kind "SharedLib"
    location (workspace_root .. "/vs2022/Oxygen")
    files {
        "Base/**.h",
        "Base/**.cpp",
        "Core/**.h",
        "Core/**.cpp",
        "Renderers/Common/**.h",
        "Renderers/Common/**.cpp",
        "ImGui/**.h",
        "ImGui/**.cpp",
        "Input/**.h",
        "Input/**.cpp",
        "Platform/**.h",
        "Platform/**.cpp",
        "World/**.h",
        "World/**.cpp",
        "api_export.h"
    }
    removefiles {
        "**/Test/**",
        "**/*_test.*",
    }
    defines { "OXYGEN_EXPORTS" }
    links { "imgui" .. link_lib_suffix }

group "Oxygen Base Tests"
    -- Base Test
    project "Oxygen.Base.Platform_Tests"
        kind "ConsoleApp"
        location (workspace_root .. "/vs2022/Oxygen/Tests")
        files {
            "Base/Test/Platform_test.cpp",
            "Base/Test/main.cpp",
        }
        links { "Oxygen", "gtest" }

    -- Base Test
    project "Oxygen.Base.Types_Tests"
        kind "ConsoleApp"
        location (workspace_root .. "/vs2022/Oxygen/Tests")
        files {
            "Base/Test/Types_test.cpp",
            "Base/Test/main.cpp",
        }
        links { "Oxygen", "gtest" }

    -- Base Test
    project "Oxygen.Base.Time_Tests"
        kind "ConsoleApp"
        location (workspace_root .. "/vs2022/Oxygen/Tests")
        files {
            "Base/Test/Time_test.cpp",
            "Base/Test/main.cpp",
        }
        links { "Oxygen", "gtest", "gmock" }

    -- Base Test
    project "Oxygen.Base.StringUtils_Tests"
        kind "ConsoleApp"
        location (workspace_root .. "/vs2022/Oxygen/Tests")
        files {
            "Base/Test/StringUtils_test.cpp",
            "Base/Test/main.cpp",
        }
        links { "Oxygen", "gtest" }

    -- Base Test
    project "Oxygen.Base.Macros_Tests"
        kind "ConsoleApp"
        location (workspace_root .. "/vs2022/Oxygen/Tests")
        files {
            "Base/Test/Macros_test.cpp",
            "Base/Test/main.cpp",
        }
        links { "Oxygen", "gtest" }

    -- Base Test
    project "Oxygen.Base.Resource_Tests"
        kind "ConsoleApp"
        location (workspace_root .. "/vs2022/Oxygen/Tests")
        files {
            "Base/Test/ResourceHandle_test.cpp",
            "Base/Test/ResourceTable_test.cpp",
            "Base/Test/Resource_test.cpp",
            "Base/Test/main.cpp",
        }
        links { "Oxygen", "gtest" }

    -- Base Test
    project "Oxygen.Base.Serio_Tests"
        kind "ConsoleApp"
        location (workspace_root .. "/vs2022/Oxygen/Tests")
        files {
            "Base/Test/Mocks/MockStream.h",
            "Base/Test/FileStream_test.cpp",
            "Base/Test/Reader_test.cpp",
            "Base/Test/Writer_test.cpp",
            "Base/Test/MemoryStream_test.cpp",
            "Base/Test/main.cpp",
        }
        links { "Oxygen", "gtest" }

    -- Base Test
    filter { "system:Windows" }
        project "Oxygen.Base.WinHelpers_Tests"
            kind "ConsoleApp"
            location (workspace_root .. "/vs2022/Oxygen/Tests")
            files {
                "Base/Test/Exceptions_test.cpp",
                "Base/Test/ComError_test.cpp",
                "Base/Test/main.cpp",
            }
            links { "Oxygen", "gtest" }

    -- Base Test
    project "Oxygen.Graphics.Common_Tests"
        kind "ConsoleApp"
        location (workspace_root .. "/vs2022/Oxygen/Tests")
        files {
            "Renderers/Common/Test/ShaderByteCode_test.cpp",
            "Renderers/Common/Test/main.cpp",
        }
        links { "Oxygen", "gtest" }
group ""
