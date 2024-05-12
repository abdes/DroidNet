// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

using System.Reactive.Linq;
using System.Reactive.Threading.Tasks;
using Oxygen.Editor.ProjectBrowser.Services;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;

public class KnownLocation(
    Storage.KnownLocations key,
    string name,
    string location,
    NativeStorageProvider localStorage,
    IProjectsService projectsService)
{
    public Storage.KnownLocations Key { get; init; } = key;

    public string Name { get; } = name;

    public string Location { get; } = location;

    public IObservable<IStorageItem> GetItems(
        ProjectItemKind kind = ProjectItemKind.All,
        CancellationToken cancellationToken = default)
#pragma warning disable IDE0072 // Add missing cases
        => this.Key switch
        {
            Storage.KnownLocations.RecentProjects
                => localStorage.GetLogicalDrives()
                    .ToObservable()
                    .SelectMany(drive => localStorage.GetFolderFromPathAsync(drive, cancellationToken).ToObservable()),

            Storage.KnownLocations.ThisComputer
                => projectsService.GetRecentlyUsedProjects(cancellationToken)
                    .Where(projectInfo => projectInfo.Location is not null)
                    .SelectMany(
                        projectInfo => localStorage
                            .GetFolderFromPathAsync(projectInfo.Location!, cancellationToken)
                            .ToObservable()),
            _
                => localStorage.GetItemsAsync(this.Location, kind, cancellationToken).ToObservable(),
        };
#pragma warning restore IDE0072 // Add missing cases
}
