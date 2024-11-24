# Oxygen.Editor.Core

Oxygen.Editor.Core is a .NET library that provides core functionalities for the Oxygen Editor. This library includes utilities for path finding, input validation, and other essential services required by the Oxygen Editor.

## Features

- **Path Finding**: Provides methods to find various system, user, and application paths.
- **Input Validation**: Includes helpers for validating input, such as file names.

## Installation

To install the Oxygen.Editor.Core library, you can use the NuGet Package Manager:

```sh
dotnet add package Oxygen.Editor.Core
```

## Usage

### Path Finding

The `PathFinder` class provides methods to find various system, user, and application paths. It uses the `IFileSystem` abstraction for path operations.

#### Example

```csharp
using System.IO.Abstractions;
using DroidNet.Config;
using Oxygen.Editor.Core.Services;

var fileSystem = new FileSystem();
var pathFinderConfig = new PathFinderConfig
{
    Mode = "real",
    ApplicationName = "OxygenEditor",
    CompanyName = "Oxygen"
};

var pathFinder = new PathFinder(fileSystem, pathFinderConfig);

Console.WriteLine(pathFinder.UserDocuments);
Console.WriteLine(pathFinder.LocalAppData);
```

### Input Validation

The `InputValidation` class provides helpers for validating input, such as file names.

#### Example

```csharp
using Oxygen.Editor.Core;

string fileName = "example.txt";
bool isValid = InputValidation.IsValidFileName(fileName);

Console.WriteLine($"Is '{fileName}' a valid file name? {isValid}");
```
