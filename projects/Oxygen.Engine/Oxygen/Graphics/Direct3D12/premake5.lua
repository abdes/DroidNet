-- Direct3D12 Renderer
project "Oxygen.Graphics.Direct3D12"
    language "C++"
    kind "SharedLib"
    location (workspace_root .. "/vs2022/Oxygen/Graphics/Direct3D12")
    files {
        "**.h",
        "**.cpp",
        "Shaders/**.hlsl",
    }
    files {
        "Shaders/**.hlsl",
    }
    removefiles { "test/**", "**_delete_*" }
    defines { "OXYGEN_D3D12_EXPORTS" }
    libdirs {
        workspace_root .. "/packages/DXC/lib/" .. default_arch()
    }
    links {
        "Oxygen",
        "d3d12",
        "dxgi",
        "dxguid",
        "dxcompiler",
        "imgui" .. link_lib_suffix
     }

    local dxc_bin_path = workspace_root .. "/packages/DXC/bin/" .. default_arch()
    local dxc_exe = dxc_bin_path .. "/dxc.exe"
    print("-- Using DXC Compiler: " .. dxc_exe)
    filter { "files:**.hlsl" }
        buildmessage "Using DXC Compiler %{dxc_exe}"
        buildcommands {
            'echo "Compiling vertex shader: %{file.abspath}"',
            string.format('"%s" -T vs_6_8 -E VS -Fo "%%{file.directory}/%%{file.basename}_vs.cso" "%%{file.abspath}"', dxc_exe),
            'echo "Compiling pixel shader: %{file.abspath}"',
            string.format('"%s" -T ps_6_8 -E PS -Fo "%%{file.directory}/%%{file.basename}_ps.cso" "%%{file.abspath}"', dxc_exe)
        }
        buildoutputs {
            "%{file.directory}/%{file.basename}_vs.cso",
            "%{file.directory}/%{file.basename}_ps.cso"
        }
    filter {}

    -- Add post-build command to copy DXC files
    local target_path = "%{cfg.buildtarget.directory}"

    postbuildcommands {
        -- Use xcopy to copy files if they are newer
        string.format('cmd /c "xcopy /D /Y /Q "%s\\*" "%s""',
            path.translate(dxc_bin_path),
            path.translate(target_path))
    }
    postbuildmessage "Copying DXC files if newer..."
