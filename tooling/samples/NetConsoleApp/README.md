# DroidNet.NetConsoleApp

This is the .NET console application sample for DroidNet that uses the [DroidNet.NetClassLibrary](https://github.com/droidnet/dotnet/tree/main/tooling/samples/NetClassLibrary) as a dependency.

## Table of Contents

1. [Introduction](#introduction)
2. [Project Structure](#project-structure)
3. [Build Output](#build-output)
4. **[Customization](#customization)**

## Introduction

This project demonstrates how to create a simple .NET console application with dependencies.

## Project Structure

The project has the following structure:

```
- src/
  - NetConsoleApp.csproj
```

## Build Output

After building the project, you'll find the output files in the `bin/Debug/net9.0/` folder:

- **DroidNet.NetConsoleApp.exe**: The executable file containing the compiled code.

Additionally, you'll find the following notable files in the `obj/Debug/net9.0/` folder:

- **NetConsoleApp.AssemblyInfo.cs**: Generated AssemblyInfo.cs.
- **DroidNet.NetConsoleApp.Version.cs**: Generated assembly version info.

## Customization

To use this project template for a real console application, follow these steps to customize it:

1. **Change the .csproj file to use the real project name**:
	* Rename the `src/NetConsoleApp.csproj` file to match your desired project name, e.g., `src/YourAppName.csproj`.

2. **Change the RootNamespace**:
	* Open the `src/NetConsoleApp.csproj` file (replace `NetConsoleApp` with your desired project name).
	* Update the `<RootNamespace>` property with your desired namespace.
	```xml
	<PropertyGroup>
	  <OutputType>Exe</OutputType>
	  <TargetFramework>net9.0</TargetFramework>

	  <IsPackable>False</IsPackable>

	  <RootNamespace>DroidNet.YourAppName</RootNamespace>
	</PropertyGroup>
	```

3. **Change the open.cmd script**:
	* Update the `open.cmd` script located in the project root folder to use the real project name.
	```bat
    SET COMMAND=dotnet slngen -d . -o YourAppName.sln --folders false .\**\*.csproj
	```

4. **Update references**:
	* Open the `src/YourAppName.csproj` file.
	* Update the `<RootNamespace>` property with your desired namespace and replace `NetConsoleApp` with your real console app project name, and update the `<ProjectReference>` path to point to your actual library project.
	```xml
	<Project Sdk="Microsoft.NET.Sdk">

	  <PropertyGroup>
	    <OutputType>Exe</OutputType>
	    <TargetFramework>net9.0</TargetFramework>

	    <IsPackable>False</IsPackable>

	    <RootNamespace>$(RootNamespace).YourAppName</RootNamespace>
	  </PropertyGroup>

	  <ItemGroup>
	    <ProjectReference Include="..\path\to\your\libraries\src\YourLibrary.csproj" />
	  </ItemGroup>

	</Project>
	```

5. **Update `Program.cs` file**:
	* Open the `src/Program.cs` file.
	* Update the namespace declaration to match your real console app's namespace and update the using directive to import your actual library.
	```csharp
	namespace DroidNet.YourAppName;

	using YourLibrary.Namespace; // Replace with your actual library's namespace

	internal static class Program
	{
		public static void Main(string[] args)
		{
			// ... rest of the code ...
		}
	}
	```

After completing these customization steps, the project is ready for use as a real .NET console application that depends on your actual library.
