--
-- Editor interop projects are a mix of C++/CLI and C# projects. This script will
-- use premake5 the project for the C++/CLI part, but because of the limited support
-- for C# projects in premake5, we will use the externalproject function to include
-- the C# projects in the solution.
--

-- Again, deal with the limited support of C++/CLI projects in premake5
local function FixManagedProject(cfg)
     premake.w('<EnableManagedPackageReferenceSupport>true</EnableManagedPackageReferenceSupport>')
     premake.w('<WindowsTargetPlatformMinVersion>10.0.26100.4654</WindowsTargetPlatformMinVersion>')
end

project "Oxygen.Editor.Interop"
     kind "SharedLib"
     location (workspace_root .. "/vs2022/Interop")
     language "C++"
     clr "netcore"
     dotnetframework "net8.0"
     files {
          "src/**.h",
          "src/**.cpp"
     }
     removefiles { "test/**" }
     links { "Oxygen" }

     require('vstudio')
     local vs = premake.vstudio.vc2010
     premake.override(premake.vstudio.vc2010.elements, "globals", function(base, prj)
          if prj.name == "Oxygen.Editor.Interop" then
               local calls = base(prj)
               table.insertafter(calls, vs.globals, FixManagedProject)
               return calls
          else
               return base(prj)
          end
   end)

-- Include C# Test project from existing .csproj file
externalproject "Oxygen.Editor.Interop.Tests"
    location (workspace_root .. "/Oxygen.Editor.Interop/test")
    kind "SharedLib"
    language "C#"

-- Include TestHelpers dependency for the C# Test project
externalproject "TestHelpers"
     location (workspace_root .. "/../TestHelpers/src")
     kind "SharedLib"
     language "C#"
