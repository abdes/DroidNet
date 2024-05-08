// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Storage;

public interface IStorageProvider
{
    Task<IFolder> GetFolderFromPathAsync(string path, CancellationToken cancellationToken = default);

    IAsyncEnumerable<IStorageItem> GetItemsAsync(
        string path,
        ProjectItemKind kind = ProjectItemKind.All,
        CancellationToken cancellationToken = default);
}
