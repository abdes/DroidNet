// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage.Native;

public class NativeFile(
    NativeStorageProvider storageProvider,
    string name,
    string path,
    string parentPath,
    DateTime lastModifiedTime) : IDocument
{
    public string ParentPath { get; } = parentPath;

    public string Name { get; } = name;

    public string Location { get; } = path;

    public DateTime LastModifiedTime { get; } = lastModifiedTime;

    internal NativeStorageProvider StorageProvider => storageProvider;
}
