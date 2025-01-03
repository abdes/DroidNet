-- Graphics backend layer
group "Graphics"
    -- dofile("Common/premake5.lua") -- Embedded into the Oxygen project
    filter { "system:Windows" }
        dofile("Direct3D12/premake5.lua")
    filter {}
    dofile("Loader/premake5.lua")
group ""
