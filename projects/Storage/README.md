# DroidNet Storage

## Project Description

DroidNet Storage provides a robust and flexible abstraction for managing storage items such as folders and documents. This module offers a comprehensive set of tools for creating, deleting, moving, copying, and enumerating storage items, with a focus on path normalization, error handling, and asynchronous operations.

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

### Atomic authored-file saves

`IStorageProvider.AtomicFiles` exposes `IAtomicFileStore`. Read a `FileSnapshot`
when opening a file and retain its `FileVersion`. Pass that version on subsequent
writes; use `FileVersion.Missing` to create a new destination without overwriting
an existing asset. Versions describe the existence and SHA-256 digest of the
complete bytes. Document revisions, history and dirty state remain the caller's
responsibility.

The native implementation copies the submitted bytes before its first await,
acquires an exclusive destination write lease, and compares the baseline before
writing. It creates a unique `.oxygen-*.tmp` file in the destination directory,
writes and flushes the complete payload to disk, closes the temporary handle,
rechecks the baseline, and publishes with a same-directory rename. Concurrent
editor writes, changed/inaccessible baselines and target collisions raise
`StorageWriteConflictException`. External tools must not race an editor's final
replacement; this contract does not support collaborative simultaneous writes.

On local Windows filesystems with atomic same-directory rename, process loss
before publication leaves the previous destination intact (or still missing for
a first save). After publication, the complete new file is authoritative. This
contract covers process interruption; it does not promise universal hardware
power-loss durability or equivalent behavior on remote/custom filesystems.
Cancellation is checked before publication and never rolls back a successful
rename.

Ordinary write, flush and replacement failures remove only the temporary file
owned by that attempt. A process interruption can leave an orphan `.tmp`; it is
never an authored asset, and a later save uses another unique temporary name.
The OS releases the exclusive `.oxygen-write.lock` handle when the process ends.
No autosave or unsaved-content recovery is provided.

Storage tests exercise existing and first saves, injected I/O failures,
cancellation, real replacement denial, baseline changes and competing writers.
The `Storage.AtomicWriteProbe` child process terminates without cleanup during a
partial write, before flush, before replacement, and after publication; the
parent verifies the reopened bytes and a subsequent successful save.

### Prerequisites

- .NET 9.0 SDK or later
- Windows 10.0.26100.0 or later

### Installation

Add a reference to the DroidNet.Storage NuGet package:

```xml
<ItemGroup>
    <PackageReference Include="DroidNet.Storage" Version="*" />
</ItemGroup>
```

### Basic Setup

```csharp
using DroidNet.Storage;
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
└── Storage.Tests.csproj # MSTest test suite
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
MSBuild.exe src\Storage.csproj /t:Build /m /v:minimal
```

### Running Tests

Execute the test suite:

```powershell
MSBuild.exe tests\Storage.Tests.csproj /t:Test /m /v:minimal
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
- **Test Location**: `tests/Storage.Tests.csproj`
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
