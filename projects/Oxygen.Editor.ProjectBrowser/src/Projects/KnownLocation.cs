// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;

namespace Oxygen.Editor.ProjectBrowser.Projects;

/// <summary>
/// Represents a known location within the project browser, providing methods to retrieve storage items from the location.
/// </summary>
/// <param name="key">The key identifying the known location.</param>
/// <param name="name">The name of the known location.</param>
/// <param name="location">The path of the known location.</param>
/// <param name="storage">The storage provider used to access storage items.</param>
/// <param name="projectBrowserService">The project browser service used to access recently used projects.</param>
public class KnownLocation(
    KnownLocations key,
    string name,
    string location,
    NativeStorageProvider storage,
    IProjectBrowserService projectBrowserService)
{
    /// <summary>
    /// Gets the key identifying the known location.
    /// </summary>
    public KnownLocations Key { get; init; } = key;

    /// <summary>
    /// Gets the name of the known location.
    /// </summary>
    public string Name { get; } = name;

    /// <summary>
    /// Gets the path of the known location.
    /// </summary>
    public string Location { get; } = location;

    /// <summary>
    /// Asynchronously retrieves the storage items from the known location.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token to observe while waiting for the task to complete.</param>
    /// <returns>An asynchronous enumerable of storage items from the known location.</returns>
    public async IAsyncEnumerable<IStorageItem> GetItems([EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        switch (this.Key)
        {
            case KnownLocations.RecentProjects:
                await foreach (var item in projectBrowserService.GetRecentlyUsedProjectsAsync(cancellationToken)
                                   .ConfigureAwait(false))
                {
                    if (cancellationToken.IsCancellationRequested)
                    {
                        yield break;
                    }

                    if (item.Location is not null)
                    {
                        yield return await storage.GetFolderFromPathAsync(item.Location, cancellationToken).ConfigureAwait(true);
                    }
                }

                break;

            case KnownLocations.ThisComputer:
                foreach (var drive in storage.GetLogicalDrives())
                {
                    if (cancellationToken.IsCancellationRequested)
                    {
                        yield break;
                    }

                    yield return await storage.GetFolderFromPathAsync(drive, cancellationToken).ConfigureAwait(true);
                }

                break;

            case KnownLocations.OneDrive:
            case KnownLocations.Downloads:
            case KnownLocations.Documents:
            case KnownLocations.Desktop:
            default:
                var thisFolder = await storage.GetFolderFromPathAsync(this.Location, cancellationToken).ConfigureAwait(true);
                await foreach (var item in thisFolder.GetItemsAsync(cancellationToken).ConfigureAwait(true))
                {
                    if (cancellationToken.IsCancellationRequested)
                    {
                        yield break;
                    }

                    yield return item;
                }

                break;
        }
    }
}
