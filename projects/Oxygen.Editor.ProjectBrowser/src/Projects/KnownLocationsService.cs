// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.InteropServices;
using DroidNet.Resources;
using Oxygen.Editor.Storage.Native;

public class KnownLocationsService : IKnownLocationsService
{
    private static readonly ConcurrentDictionary<KnownLocations, KnownLocation> CachedLocations = new();

    private readonly NativeStorageProvider localStorage;
    private readonly IProjectsService projectsService;

    public KnownLocationsService(NativeStorageProvider localStorage, IProjectsService projectsService)
        => (this.localStorage, this.projectsService) = (localStorage, projectsService);

    public async Task<KnownLocation?> ForKeyAsync(KnownLocations key, CancellationToken cancellationToken = default)
    {
        if (CachedLocations.TryGetValue(key, out var location))
        {
            return location;
        }

        location = key switch
        {
            KnownLocations.RecentProjects => new KnownLocation(
                key,
                "Recent Projects".TryGetLocalizedMine(),
                string.Empty,
                this.localStorage,
                this.projectsService),
            KnownLocations.ThisComputer => new KnownLocation(
                key,
                "This Computer".TryGetLocalizedMine(),
                string.Empty,
                this.localStorage,
                this.projectsService),
            KnownLocations.OneDrive => await this.FromLocalFolderPathAsync(
                    key,
                    SHGetKnownFolderPath(new Guid("A52BBA46-E9E1-435f-B3D9-28DAA648C0F6"), 0),
                    cancellationToken)
                .ConfigureAwait(true),
            KnownLocations.Downloads => await this.FromLocalFolderPathAsync(
                    key,
                    SHGetKnownFolderPath(new Guid("374DE290-123F-4565-9164-39C4925E467B"), 0),
                    cancellationToken)
                .ConfigureAwait(true),
            KnownLocations.Documents => await this.FromLocalFolderPathAsync(
                    key,
                    Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                    cancellationToken)
                .ConfigureAwait(true),
            KnownLocations.Desktop => await this.FromLocalFolderPathAsync(
                    key,
                    Environment.GetFolderPath(Environment.SpecialFolder.Desktop),
                    cancellationToken)
                .ConfigureAwait(true),
            _ => throw new ArgumentOutOfRangeException(nameof(key), key, message: null),
        };

        if (location == null)
        {
            return null;
        }

        var added = CachedLocations.TryAdd(key, location);
        Debug.Assert(added, $"Adding the cached location for key [{key}] should have succeeded");

        return location;
    }

#pragma warning disable SYSLIB1054 // Use 'LibraryImportAttribute' instead of 'DllImportAttribute' to generate P/Invoke marshalling code at compile time
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, PreserveSig = false)]
    private static extern string SHGetKnownFolderPath(
        [MarshalAs(UnmanagedType.LPStruct)] Guid refToGuid,
        uint dwFlags,
        nint hToken = default);
#pragma warning restore SYSLIB1054 // Use 'LibraryImportAttribute' instead of 'DllImportAttribute' to generate P/Invoke marshalling code at compile time

    private async Task<KnownLocation?> FromLocalFolderPathAsync(
        KnownLocations key,
        string path,
        CancellationToken cancellationToken)
    {
        var folder = await this.localStorage.GetFolderFromPathAsync(path, cancellationToken).ConfigureAwait(true);
        return folder != null
            ? new KnownLocation(key, folder.Name, folder.Location, this.localStorage, this.projectsService)
            : null;
    }
}
