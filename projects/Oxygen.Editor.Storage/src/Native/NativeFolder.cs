// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage.Native;

public class NativeFolder(NativeStorageProvider storageProvider, string name, string path, DateTime lastModifiedTime)
    : IFolder
{
    public string Name { get; } = name;

    public string Location { get; } = path;

    public DateTime LastModifiedTime { get; } = lastModifiedTime;

    internal NativeStorageProvider StorageProvider => storageProvider;

    public IAsyncEnumerable<IStorageItem> GetItemsAsync(
        ProjectItemKind kind = ProjectItemKind.All,
        CancellationToken cancellationToken = default)
        => storageProvider.GetItemsAsync(this.Location, kind, cancellationToken);
}
