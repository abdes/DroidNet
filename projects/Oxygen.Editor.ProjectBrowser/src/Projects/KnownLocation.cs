// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

using System.Diagnostics;
using System.Runtime.CompilerServices;
using Oxygen.Editor.ProjectBrowser.Services;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;

public class KnownLocation
{
    private readonly NativeStorageProvider localStorage;
    private readonly IProjectsService projectsService;

    internal KnownLocation(
        Storage.KnownLocations key,
        string name,
        string location,
        NativeStorageProvider localStorage,
        IProjectsService projectsService)
    {
        this.Key = key;
        this.Name = name;
        this.Location = location;

        this.localStorage = localStorage;
        this.projectsService = projectsService;
    }

    public Storage.KnownLocations Key { get; init; }

    public string Name { get; }

    public string Location { get; }

    public IAsyncEnumerable<IStorageItem> GetItemsAsync(
        ProjectItemKind kind = ProjectItemKind.All,
        CancellationToken cancellationToken = default)
#pragma warning disable IDE0072 // Add missing cases
        => this.Key switch
        {
            Storage.KnownLocations.RecentProjects => this.GetRecentProjectsAsync(cancellationToken),
            Storage.KnownLocations.ThisComputer => this.GetLocalDrivesAsync(cancellationToken),
            _ => this.localStorage.GetItemsAsync(this.Location, kind, cancellationToken),
        };
#pragma warning restore IDE0072 // Add missing cases

    private async IAsyncEnumerable<IStorageItem> GetLocalDrivesAsync(
        [EnumeratorCancellation] CancellationToken cancellationToken)
    {
        foreach (var drive in this.localStorage.GetLogicalDrives())
        {
            var folder = await this.localStorage.GetFolderFromPathAsync(drive, cancellationToken).ConfigureAwait(true);
            Debug.Assert(folder != null, nameof(folder) + $" for logical drive [{drive}] != null");
            yield return folder;
        }
    }

    private async IAsyncEnumerable<IStorageItem> GetRecentProjectsAsync(
        [EnumeratorCancellation] CancellationToken cancellationToken)
    {
        await foreach (var project in this.projectsService.GetRecentlyUsedProjectsAsync(cancellationToken)
                           .ConfigureAwait(true))
        {
            if (project.Location == null)
            {
                continue;
            }

            yield return await this.localStorage
                .GetFolderFromPathAsync(project.Location, cancellationToken)
                .ConfigureAwait(true);
        }
    }
}
