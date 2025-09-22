-- Editor Interop Tests (C++/CLI)
project "Oxygen.Editor.Interop.Tests"
     kind "SharedLib"
     location (workspace_root .. "/vs2022/Interop")
     language "C#"
     architecture "x64"
     dotnetframework "net8.0" -- or the appropriate .NET version

     -- Set the root namespace
     namespace ("Oxygen.Editor.Interop.Tests")

     -- Add project references
     links {
          "Oxygen.Editor.Interop"
     }
     files {
          "**.cs"
     }
