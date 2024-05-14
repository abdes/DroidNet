// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

using System.Runtime.CompilerServices;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;

public class KnownLocation(
    KnownLocations key,
    string name,
    string location,
    NativeStorageProvider storage,
    IProjectsService projectsService)
{
    public KnownLocations Key { get; init; } = key;

    public string Name { get; } = name;

    public string Location { get; } = location;

    public async IAsyncEnumerable<IStorageItem> GetItems(
        ProjectItemKind kind = ProjectItemKind.All,
        [EnumeratorCancellation]
        CancellationToken cancellationToken = default)
    {
        if (this.Key == KnownLocations.RecentProjects)
        {
            await foreach (var item in projectsService.GetRecentlyUsedProjectsAsync(cancellationToken)
                               .ConfigureAwait(false))
            {
                if (cancellationToken.IsCancellationRequested)
                {
                    yield break;
                }

                if (item.Location is not null)
                {
                    yield return await storage.GetFolderFromPathAsync(item.Location, cancellationToken)
                        .ConfigureAwait(false);
                }
            }
        }
        else if (this.Key == KnownLocations.ThisComputer)
        {
            foreach (var drive in storage.GetLogicalDrives())
            {
                if (cancellationToken.IsCancellationRequested)
                {
                    yield break;
                }

                yield return await storage.GetFolderFromPathAsync(drive, cancellationToken).ConfigureAwait(false);
            }
        }
        else
        {
            await foreach (var item in storage.GetItemsAsync(this.Location, kind, cancellationToken)
                               .ConfigureAwait(false))
            {
                if (cancellationToken.IsCancellationRequested)
                {
                    yield break;
                }

                yield return item;
            }
        }
    }
}
