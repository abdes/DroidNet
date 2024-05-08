// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage.Native;

public class NativeNestedFolder(
    NativeStorageProvider storageProvider,
    string name,
    string path,
    string parentPath,
    DateTime lastModifiedTime) : NativeFolder(storageProvider, name, path, lastModifiedTime), INestedItem
{
    public string ParentPath { get; } = parentPath;
}
