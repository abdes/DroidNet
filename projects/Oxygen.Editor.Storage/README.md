# Oxygen Editor Document Storage Layer

## Project Description

The Oxygen Editor Document Storage Layer provides a robust and flexible abstraction for managing storage items such as folders and documents within the Oxygen Editor. This module offers a comprehensive set of tools for creating, deleting, moving, copying, and enumerating storage items, with a focus on path normalization, error handling, and asynchronous operations.

## Technology Stack

- **Framework**: .NET 9.0 for Windows
- **Platform**: Windows 10.0.26100.0+
- **Language**: C# 13 (with preview features)
- **File System Abstraction**: `IFileSystem` from [Testably.Abstractions](https://github.com/Testably/Abstractions)
- **Encoding**: UTF-8 for all text documents

## Project Architecture

The storage layer follows a contract-based design pattern with clear separation of concerns:

- **Provider Pattern**: `IStorageProvider` abstracts path normalization and item retrieval
- **Abstraction Interfaces**: `IFolder`, `IDocument`, `IStorageItem`, and `INestedItem` define storage contracts
- **Native Implementation**: `NativeStorageProvider`, `NativeFolder`, and `NativeFile` implement these contracts using the file system
- **Error Handling**: Hierarchical exception model with specific exception types for different scenarios

### Key Design Principles

- **Minimal Exception Types**: Only throw exceptions when the UI can meaningfully react (`ItemNotFoundException`, `TargetExistsException`) or encapsulate in `StorageException`
- **Path Normalization**: All paths are normalized to absolute, canonical form
- **Asynchronous First**: All I/O operations are async to prevent UI blocking
- **UTF-8 Standard**: Consistent encoding for all text documents

## Getting Started

### Prerequisites

- .NET 9.0 SDK or later
- Windows 10.0.26100.0 or later

### Installation

Add a reference to the Oxygen.Editor.Storage NuGet package:

```xml
<ItemGroup>
    <PackageReference Include="Oxygen.Editor.Storage" Version="*" />
</ItemGroup>
```

Or via the dotnet CLI:

```powershell
dotnet add package Oxygen.Editor.Storage
```

### Basic Setup

```csharp
using Oxygen.Editor.Storage;
using Testably.Abstractions;

// Create a file system abstraction
IFileSystem fileSystem = new FileSystem();

// Create a storage provider
IStorageProvider storageProvider = new NativeStorageProvider(fileSystem);

// Normalize a path
string normalizedPath = storageProvider.Normalize("./MyFolder");

// Get a folder
INestedFolder folder = await storageProvider.GetFolderFromPathAsync(normalizedPath);
```

## Project Structure

```text
src/
├── Interfaces/
│   ├── IStorageProvider.cs     # Core abstraction for storage operations
│   ├── IStorageItem.cs         # Base interface for storage items
│   ├── IFolder.cs              # Folder operations contract
│   ├── IDocument.cs            # Document operations contract
│   ├── INestedItem.cs          # Nested item properties
│   └── INestedFolder.cs        # Nested folder contract
├── Exceptions/
│   ├── StorageException.cs     # Base storage exception
│   ├── ItemNotFoundException.cs # Item not found error
│   ├── TargetExistsException.cs # Target already exists error
│   └── InvalidPathException.cs # Invalid path error
└── Native/
    ├── NativeStorageProvider.cs # File system implementation
    ├── NativeFolder.cs          # Folder implementation
    └── NativeFile.cs            # Document implementation

tests/
└── Oxygen.Editor.Storage.Tests/ # MSTest test suite
```

## Key Features

### Core Storage Operations

- **Item Retrieval**: Get folders and documents by path with normalized resolution
- **Create Operations**: Create folders, documents, or nested hierarchies
- **Delete Operations**: Remove items with proper error handling
- **Copy & Move**: Duplicate or relocate items with collision detection
- **Enumeration**: List items within folders recursively or non-recursively

### Path Management

- **Normalization**: Convert paths to absolute, canonical form
- **Path Combination**: Safely combine base and relative paths
- **Validation**: Comprehensive path validation with specific error reporting

### Error Handling

- **ItemNotFoundException**: Thrown when an operation targets a non-existent item
- **TargetExistsException**: Thrown when creating/moving to a location where an item already exists
- **StorageException**: Encapsulates underlying file system errors with original exception details
- **InvalidPathException**: Thrown for malformed or invalid paths

## Development Workflow

### Building

Build the project using the solution or directly via project file:

```powershell
dotnet build src/Oxygen.Editor.Storage.csproj
```

### Running Tests

Execute the test suite:

```powershell
dotnet test tests/Oxygen.Editor.Storage.Tests/Oxygen.Editor.Storage.Tests.csproj
```

### Code Standards

This project follows the DroidNet repository code standards:

- **Access Modifiers**: Explicit modifiers on all types and members
- **Null Safety**: Nullable reference types enabled with strict analysis
- **Code Style**: C# 13 with preview features, consistent with `.editorconfig`
- **Naming**: Follow Microsoft guidelines and existing conventions
- **Documentation**: XML documentation comments on all public APIs

## Testing

The project uses **MSTest** with the AAA (Arrange-Act-Assert) pattern:

- **Test Framework**: MSTest 4.0
- **Test Location**: `tests/Oxygen.Editor.Storage.Tests/`
- **Naming Convention**: `MethodName_Scenario_ExpectedBehavior`
- **Assertion Library**: AwesomeAssertions for fluent assertions
- **Mocking**: Moq for interface mocking and file system abstraction

### Test Categories

- **Unit Tests**: Core interface and method behavior validation
- **Integration Tests**: Multi-component interaction scenarios
- **Error Tests**: Exception handling and edge cases

## Usage Guidelines

### Path Normalization

Always normalize paths before using them:

```csharp
string normalizedPath = storageProvider.Normalize("./MyFolder/../MyFolder");
```

### Error Handling

Implement contextual error handling:

```csharp
try
{
    await document.DeleteAsync();
}
catch (ItemNotFoundException)
{
    // Item was already deleted or doesn't exist
    uiContext.RemoveItemFromDisplay(document);
}
catch (StorageException ex)
{
    // Report the error message to the user
    uiContext.ShowError($"Storage error: {ex.Message}");
}
```

### Asynchronous Operations

Always use async methods to prevent UI blocking:

```csharp
IDocument document = await folder.GetDocumentAsync("file.txt");
string content = await document.ReadAllTextAsync();
await document.WriteAllTextAsync("Updated content");
```

### Text Encoding

All text operations use UTF-8 encoding automatically:

```csharp
// UTF-8 encoding is applied automatically
await document.WriteAllTextAsync("Hello, World!");
string content = await document.ReadAllTextAsync();
```

## Example Usage

```csharp
/// <summary>
/// Complete example demonstrating core storage layer operations.
/// </summary>
public async Task ExampleUsage()
{
    // Initialize storage provider
    IFileSystem fileSystem = new FileSystem();
    IStorageProvider storageProvider = new NativeStorageProvider(fileSystem);

    // Get or create a folder
    INestedFolder folder = await storageProvider.GetFolderFromPathAsync("C:/ExampleFolder");
    if (!await folder.ExistsAsync())
    {
        await folder.CreateAsync();
    }

    // Create a document within the folder
    IDocument document = await folder.GetDocumentAsync("ExampleDocument.txt");
    await document.WriteAllTextAsync("Hello, World!");

    // Read the document content
    string content = await document.ReadAllTextAsync();
    Console.WriteLine(content);

    // Copy the document
    IDocument copy = await document.CopyAsync("ExampleDocument_Copy.txt");

    // Enumerate items in the folder
    IEnumerable<INestedItem> items = await folder.EnumerateItemsAsync();
    foreach (var item in items)
    {
        Console.WriteLine($"Item: {item.Path}");
    }

    // Clean up
    await document.DeleteAsync();
    await copy.DeleteAsync();
    await folder.DeleteAsync();
}
```

## Contributing

When contributing to this module:

1. Follow the C# coding standards in `.github/instructions/csharp_coding_style.instructions.md`
2. Add or update tests for all new functionality using MSTest patterns
3. Ensure all paths are normalized using `IStorageProvider.Normalize()` or `IStorageProvider.CombineAsync()`
4. Implement proper exception handling with specific exception types
5. Use UTF-8 encoding consistently for text documents
6. Keep async operations async throughout the call chain
7. Add XML documentation comments to all public APIs

## License

This project is part of the DroidNet repository and is licensed under the MIT License. See the LICENSE file in the repository root for details.
