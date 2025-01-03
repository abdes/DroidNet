newoption {
	trigger = "vcpkg-root",
	value = "path",
	description = "Path to vcpkg installation"
}

function default_arch()
	if os.istarget("linux") then return "x64" end
    if os.istarget("macosx") then return "arm64" end
	if os.istarget("windows") then return "x64" end
end

local absolute_vcpkg_path =(function()
	if _OPTIONS["vcpkg-root"] then
		return path.getabsolute(_OPTIONS["vcpkg-root"])
	end
    return os.getcwd()
end)()

function get_vcpkg_root_path(arch)
	local function vcpkg_triplet_path()
		if os.istarget("linux") then
			return "-linux-static"
		elseif os.istarget("macosx") then
			return "-osx-static"
		elseif os.istarget("windows") then
			return "-windows-static-md"
		end
	end
	return absolute_vcpkg_path .. "/vcpkg_installed/" .. arch .. vcpkg_triplet_path()
end

-- Workspace Configuration
workspace "Oxygen Engine"
    -- Get the workspace root path
    workspace_root = os.getcwd()

    configurations { "Debug", "Release" }
    location "vs2022"
    platforms { default_arch() }
    -- Output directories
    targetdir "bin/%{cfg.buildcfg}-%{cfg.platform}"
    objdir "obj/%{cfg.buildcfg}-%{cfg.platform}/%{prj.name}"

    -- Global settings
    filter "language:C++"
        cppdialect "C++20"
        characterset "Unicode"
        staticruntime "On"
        defines { "LOGURU_USE_FMTLIB=1", "FMT_HEADER_ONLY" }
        flags { "MultiProcessorCompile" }
        includedirs { workspace_root }

    filter {}

    filter { "language:C++", "system:windows" }
        defines { "WIN32", "_WINDOWS", "WIN32_LEAN_AND_MEAN", "NOMINMAX" }
        systemversion "latest"
        toolset "v143"
        vsprops { VcpkgEnabled = "false" }
    filter {}

    -- Vcpkg integration
    local full_vcpkg_root_path=get_vcpkg_root_path(default_arch())
    print("-- Vcpkg root path: " .. full_vcpkg_root_path)
    includedirs { full_vcpkg_root_path .. "/include" }
    filter { "configurations:Debug" }
        libdirs { full_vcpkg_root_path .. "/debug/lib" }
    filter { "configurations:Release" }
        libdirs { full_vcpkg_root_path .. "/lib" }
    filter {}

    -- Custom vcpkg path
    -- vcpkg_root = "vcpkg_installed"
    -- includedirs {
    --     path.join(vcpkg_root, "x64-windows-static-md/include"),
    --     path.join(vcpkg_root, "x64-windows-static/include")
    -- }
    -- libdirs {
    --     path.join(vcpkg_root, "x64-windows/lib"),
    --     path.join(vcpkg_root, "x64-windows-static/lib")
    -- }

    -- Configurations
    filter "configurations:Debug"
        runtime "Debug"
        staticruntime "Off"
        symbols "On"
        defines { "_DEBUG" }
        optimize "Off"
        buildoptions { "/utf-8" }
        includedirs { "$(ProjectsRoot)/Oxygen.Engine" }
        conformancemode "On"
        functionlevellinking "On"
        vectorextensions "AVX2"
        warnings "Extra"
        editandcontinue "On"
        defines { "VcpkgEnableManifest=true" }

    filter "configurations:Release"
        runtime "Release"
        staticruntime "Off"
        optimize "Speed"
        defines { "NDEBUG" }
        buildoptions { "/utf-8", "/GL" }
        linkoptions { "/LTCG" }
        includedirs { "$(ProjectsRoot)/Oxygen.Engine" }
        conformancemode "On"
        functionlevellinking "Off"
        vectorextensions "AVX2"
        warnings "Extra"
        editandcontinue "Off"
        defines { "VcpkgEnableManifest=true" }
        linktimeoptimization "On"

-- Define the library suffix based on the configuration
link_lib_suffix = ""
filter "configurations:Debug"
    link_lib_suffix = "d"
filter {}

-- Include project configurations
dofile("Oxygen/premake5.lua")
dofile("Oxygen/Platform/premake5.lua")
dofile("Oxygen/Graphics/premake5.lua")
dofile("Oxygen.Editor.Interop/premake5.lua")
dofile("Examples/premake5.lua")


    -- -- fix vcpkg in visual studio (set triplet)
    -- filter "action:vs*"
    --     local function vcpkg(prj)
    --         premake.w('<VcpkgTriplet Condition="\'$(Platform)\'==\'x64\'">x64-windows-static</VcpkgTriplet>')
    --         premake.w('<VcpkgEnabled>true</VcpkgEnabled>')
    --         -- premake.w('<VcpkgUseStatic>true</VcpkgUseStatic>')
    --         -- premake.w('<VcpkgUseMD>true</VcpkgUseMD>')
    --         premake.w('<VcpkgEnableManifest>true</VcpkgEnableManifest>')
    --         premake.w('<VcpkgAutoLink>false</VcpkgAutoLink>')
    --     end

    --     require('vstudio')
    --     local vs = premake.vstudio.vc2010
    --     premake.override(premake.vstudio.vc2010.elements, "globals", function(base, prj)
    --             local calls = base(prj)
    --             table.insertafter(calls, vs.globals, vcpkg)
    --             return calls
    --     end)
