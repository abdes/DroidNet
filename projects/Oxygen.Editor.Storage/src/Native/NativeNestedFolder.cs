// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage.Native;

public class NativeNestedFolder : NativeFolder, INestedFolder
{
    internal NativeNestedFolder(
        NativeStorageProvider storageProvider,
        string name,
        string path,
        string parentPath)
        : base(storageProvider, name, path)
        => this.ParentPath = parentPath;

    public string ParentPath { get; }
}
