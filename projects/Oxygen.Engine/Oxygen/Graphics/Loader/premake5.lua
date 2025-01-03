
-- Renderer Loader
project "Oxygen.Loader"
    language "C++"
    kind "StaticLib"
    location (workspace_root .. "/vs2022/Oxygen/Graphics/Loader")
    files {
        "**.h",
        "**.cpp"
    }
    removefiles { "Test/**" }
    links { "Oxygen" }
