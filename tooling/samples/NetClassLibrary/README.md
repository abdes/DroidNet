 # DroidNet.NetClassLibrary

This is the .NET class library sample for DroidNet.

## Table of Contents

1. [Introduction](#introduction)
2. [Project Structure](#project-structure)
3. [Build Output](#build-output)
4. **[Customization](#customization)**

## Introduction

This project demonstrates a simple .NET class library that can be used as a dependency in other projects within the DroidNet solution. It contains basic classes and methods to showcase functionality.

## Project Structure

The project has the following structure:

```
- src/
  - NetClassLibrary.csproj
```

## Build Output

After building the project, you'll find the output files in the `bin/Debug/net9.0/` folder:

- **DroidNet.NetClassLibrary.dll**: The main assembly file containing the compiled code.
- **DroidNet.NetClassLibrary.deps.json**: A file listing the assembly's dependencies.
- **DroidNet.NetClassLibrary.xml**: Generated XML docs for the assembly.

Additionally, you'll find the following notable files in the `obj/Debug/net9.0/` folder:

- **NetClassLibrary.AssemblyInfo.cs**: Generated AssemblyInfo.cs.
- **DroidNet.NetClassLibrary.Version.cs**: Generated assembly version info.

 You're right, I apologize for the oversight. Here's an updated customization section with the additional steps you mentioned:

## Customization

To use this project template for a real library, follow these steps to customize it:

1. **Change the .csproj file to use the real project name**:
	* Rename the `src/NetClassLibrary.csproj` file to match your desired project name, e.g., `src/YourLibrary.csproj`.

2. **Change the RootNamespace**:
	* Open the `src/NetClassLibrary.csproj` file (replace `NetClassLibrary` with your desired project name).
	* Update the `<RootNamespace>` property with your desired namespace.
	```xml
	<PropertyGroup>
	  <TargetFramework>net9.0</TargetFramework>
	  <ImplicitUsingsEnable>False</ImplicitUsingsEnable>
	  <Nullable>enable</Nullable>
	  <RootNamespace>DroidNet.YourLibrary</RootNamespace>
	</PropertyGroup>
	```

3. **Change the open.cmd script**:
	* Update the `open.cmd` script located in the project root folder to use the real project name.
	```bat
    SET COMMAND=dotnet slngen -d . -o YourLibrary.sln --folders false .\**\*.csproj
	```

4. **Update test project file name and references**:
	* Rename the `tests/NetClassLibraryTests.csproj` file to match your desired test project name, e.g., `tests/YourLibraryTests.csproj`.
	* Open the `tests/YourLibraryTests.csproj` file.
	* Update the `<RootNamespace>` property with your desired namespace and replace `NetClassLibrary` with your real library project name.
	```xml
    <Project Sdk="Microsoft.NET.Sdk">
        <PropertyGroup>
            <TargetFramework>net9.0</TargetFramework>
            <RootNamespace>$(RootNamespace).YourLibrary.Tests</RootNamespace>
        </PropertyGroup>

        <ItemGroup>
            <ProjectReference Include="..\src\YourLibrary.csproj" />
        </ItemGroup>
    </Project>
	```

5. **Update GlobalSuppressions.cs file in the test project**:
	* Open the `tests/YourLibraryTests/GlobalSuppressions.cs` file.
	* Update the namespace for exclusion to match your real library's namespace.
	```csharp
	[assembly: SuppressMessage("ReSharper", "UnusedAutoPropertyAccessor.Local", Justification = "Used in tests.", Scope = " DroidNet.YourLibrary.Tests")]
	```

6. **Change the namespace in all source code files**:
	* Update the namespace declaration in all `.cs` files to match your real library's namespace.
	```csharp
	namespace DroidNet.YourLibrary...;

	// ... rest of the code ...
	```

After completing these customization steps, the project is ready for use as a real .NET class library with its corresponding test project.
