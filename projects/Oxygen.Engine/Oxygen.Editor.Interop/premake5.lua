
-- Editor Interop (C++/CLI)
project "Oxygen.Editor.Interop"
    kind "SharedLib"
    location (workspace_root .. "/vs2022/Oxygen.Editor.Interop")
    language "C++"
    clr "netcore"
    dotnetframework "net8.0"
    files {
         "src/**.h",
         "src/**.cpp"
    }
    removefiles { "test/**" }
    links { "Oxygen" }
