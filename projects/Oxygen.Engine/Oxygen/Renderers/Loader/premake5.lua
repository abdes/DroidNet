
-- Renderer Loader
project "Oxygen.Loader"
    kind "StaticLib"
    location (workspace_root .. "/vs2022/Oxygen/Graphics/Loader")
    files {
        "**.h",
        "**.cpp"
    }
    removefiles { "test/**" }
    links { "Oxygen" }
