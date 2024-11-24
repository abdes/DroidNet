# Oxygen Editor Document Storage Layer

## Overview

The Oxygen Editor Document Storage Layer is a robust and flexible system designed to manage storage
items such as folders and documents within the Oxygen Editor. This module provides a comprehensive
set of tools for creating, deleting, moving, copying, and enumerating storage items, with a focus on
path normalization, error handling, and asynchronous operations.

## Error Handling and Exceptions

The design philosophy is to minimize the types of exceptions reported. Most of the time, there is
very little the UI can do other than reporting the error message to the user. However, there are
situations where the UI can intelligently handle the error or offer a smarter choice to the user.
These scenarios include:

- If a storage operation is attempted on an item, but the item was not found at the time the
operation was executed, an `ItemNotFoundException` can be reported. In such cases, the UI can ignore
the failed operation and update itself to remove the item from display. - If a storage operation
involves creating, copying, or moving an item and another item with the same name exists at the
target location, a `TargetExistsException` could be thrown. In such cases, the UI can offer a choice
to the user to overwrite, rename, or cancel. - Any other error should simply be encapsulated inside
a `StorageException`, with the original exception set as an `InnerException`.

## Encoding in Text Documents

All documents that have text content (such as JSON files) use the "UTF-8" encoding.

## Key Components

### Interfaces

#### `IStorageProvider`

Defines methods for normalizing paths, combining paths, and retrieving storage items (folders and
documents) from paths. It also includes methods for checking the existence of documents and folders.

#### `IFolder`

Represents a folder that can contain other folders and documents. It provides methods for creating,
deleting, and enumerating items within the folder.

#### `IDocument`

Represents a document within a folder. It provides methods for deleting, copying, moving, reading,
and writing the document.

#### `INestedItem`

Represents an item that is nested within a folder structure, providing a property for the parent
folder's path.

#### `INestedFolder`

Extends `IFolder` and `INestedItem`, representing a folder that resides within another folder.

### Classes

#### `StorageException`

A custom exception class for handling errors related to storage operations.

#### `NativeStorageProvider`

An implementation of `IStorageProvider` that uses a file system abstraction (`IFileSystem`) to
manage storage items. It includes methods for normalizing paths, retrieving folders and documents,
and checking their existence.

#### `NativeFolder`

An implementation of `IFolder` that represents a folder in the file system. It provides methods for
creating, deleting, and enumerating items within the folder.

#### `NativeFile`

An implementation of `IDocument` that represents a document in the file system. It provides methods
for deleting, copying, moving, reading, and writing the document.

## Example Usage

```csharp
/// <summary>
/// Example of how to use the NativeStorageProvider to get a folder and create a document within it.
/// </summary>
public async Task ExampleUsage()
{
    IFileSystem fileSystem = new FileSystem();
    IStorageProvider storageProvider = new NativeStorageProvider(fileSystem);

    // Get or create a folder
    IFolder folder = await storageProvider.GetFolderFromPathAsync("C:/ExampleFolder");
    await folder.CreateAsync();

    // Create a document within the folder
    IDocument document = await folder.GetDocumentAsync("ExampleDocument.txt");
    await document.WriteAllTextAsync("Hello, World!");

    // Read the document content
    string content = await document.ReadAllTextAsync();
    Console.WriteLine(content);
}
```

## Guidelines for Users and Implementors

- **Path Normalization**: Always use the `Normalize` and `NormalizeRelativeTo` methods to ensure
paths are correctly formatted and absolute.

- **Error Handling**: Catch specific exceptions like `ItemNotFoundException` and
`TargetExistsException` to provide a better user experience. For other errors, handle
`StorageException` to encapsulate the original error.

- **Asynchronous Operations**: Utilize the asynchronous methods provided to avoid blocking the UI
thread, especially for long-running operations like file I/O.

- **UTF-8 Encoding**: Ensure that all text documents are read and written using UTF-8 encoding to
maintain consistency and compatibility.

By following these guidelines and understanding the key components, you can effectively utilize the
Oxygen Editor Document Storage Layer to manage storage items within your application.
