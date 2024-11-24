// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage.Native;

/// <summary>
/// Represents a nested folder in the file system, providing methods for managing folders and documents within it.
/// </summary>
public class NativeNestedFolder : NativeFolder, INestedFolder
{
    /// <summary>
    /// Initializes a new instance of the <see cref="NativeNestedFolder"/> class.
    /// </summary>
    /// <param name="storageProvider">The storage provider used to manage the file system operations.</param>
    /// <param name="name">The name of the folder.</param>
    /// <param name="path">The full path of the folder.</param>
    /// <param name="parentPath">The path of the parent folder containing this folder.</param>
    internal NativeNestedFolder(
        NativeStorageProvider storageProvider,
        string name,
        string path,
        string parentPath)
        : base(storageProvider, name, path)
    {
        this.ParentPath = parentPath;
    }

    /// <summary>
    /// Gets the path of the parent folder containing the current folder.
    /// </summary>
    public string ParentPath { get; }
}
