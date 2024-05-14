// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

using System.ComponentModel;
using System.Diagnostics;
using System.Reactive.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Resources;
using Microsoft.Extensions.DependencyInjection;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;

/// <summary>Provides method to access and manipulate projects.</summary>
public class ProjectsService : IProjectsService
{
    private readonly IPathFinder finder;
    private readonly IProjectSource projectsSource;
    private readonly NativeStorageProvider localStorage;

    private readonly Lazy<Task<KnownLocation[]>> lazyLocations;

    public ProjectsService(IProjectSource source, IPathFinder finder, NativeStorageProvider localStorage)
    {
        this.projectsSource = source;
        this.finder = finder;
        this.localStorage = localStorage;

        this.lazyLocations = new Lazy<Task<KnownLocation[]>>(() => this.InitializeKnownLocationsAsync());
    }

    public async Task<bool> NewProjectFromTemplate(
        ITemplateInfo templateInfo,
        string projectName,
        string atLocationPath)
    {
        if (!this.CanCreateProject(projectName, atLocationPath))
        {
            Debug.WriteLine($"Cannot create a new project with name `{projectName}` at: {atLocationPath}");
            return false;
        }

        if (string.IsNullOrEmpty(templateInfo.Location))
        {
            Debug.WriteLine($"Invalid template location: {templateInfo.Location}");
            return false;
        }

        var templateDirInfo = new DirectoryInfo(templateInfo.Location!);

        IFolder? projectFolder = null;
        try
        {
            // Create project folder
            projectFolder = await this.projectsSource.CreateNewProjectFolder(projectName, atLocationPath)
                .ConfigureAwait(false);
            if (projectFolder == null)
            {
                return false;
            }

            // Copy all content from template to the new project folder
            CopyTemplateAssetsToProject();

            // Load the project info, update it, and save it
            var projectInfo
                = await this.projectsSource.LoadProjectInfoAsync(projectFolder.Location).ConfigureAwait(false);
            if (projectInfo != null)
            {
                projectInfo.Name = projectName;
                if (await this.projectsSource.SaveProjectInfoAsync(projectInfo).ConfigureAwait(false))
                {
                    // Update the recently used project entry for the project being saved
                    await TryUpdateRecentUsageAsync(projectInfo.Location!).ConfigureAwait(false);

                    // Update the last save location
                    await TryUpdateLastSaveLocation(new DirectoryInfo(projectInfo.Location!).Parent!.FullName)
                        .ConfigureAwait(false);

                    return true;
                }
            }
        }
        catch (Exception copyError)
        {
            Debug.WriteLine($"Project creation failed: {copyError.Message}");
        }

        RemoveFailedProject(projectFolder);
        return false;

        void CopyTemplateAssetsToProject()
        {
            _ = Parallel.ForEach(
                templateDirInfo.GetDirectories("*", SearchOption.AllDirectories),
                srcInfo => Directory.CreateDirectory(
                    projectFolder.Location + srcInfo.FullName[templateDirInfo.FullName.Length..]));

            _ = Parallel.ForEach(
                templateDirInfo.GetFiles("*", SearchOption.AllDirectories),
                srcInfo =>
                {
                    if (!string.Equals(srcInfo.Name, "Template.json", StringComparison.Ordinal))
                    {
                        File.Copy(
                            srcInfo.FullName,
                            projectFolder.Location + srcInfo.FullName[templateDirInfo.FullName.Length..],
                            overwrite: true);
                    }
                });
        }

        static void RemoveFailedProject(IFolder? storageLocation)
        {
            if (storageLocation == null)
            {
                return;
            }

            try
            {
                Directory.Delete(storageLocation.Location, recursive: true);
            }
            catch (Exception deleteError)
            {
                Debug.WriteLine(
                    $"An error occurred while trying to delete a failed project folder at `{storageLocation.Location}`: {deleteError.Message}");
            }
        }
    }

    public async Task<bool> LoadProjectAsync(string location)
    {
        await TryUpdateRecentUsageAsync(location).ConfigureAwait(false);

        // TODO(abdes) load the project
        return true;
    }

    public IList<QuickSaveLocation> GetQuickSaveLocations()
    {
        using var state = Ioc.Default.CreateScope().ServiceProvider.GetRequiredService<PersistentState>();

        var locations = new List<QuickSaveLocation>();

        var lastSaveLocation = state.ProjectBrowserState.LastSaveLocation;
        if (lastSaveLocation != string.Empty)
        {
            locations.Add(new QuickSaveLocation("Recently Used", lastSaveLocation));
        }

        locations.Add(new QuickSaveLocation("Personal Projects", Path.GetFullPath(this.finder.PersonalProjects)));
        locations.Add(new QuickSaveLocation("Local Projects", Path.GetFullPath(this.finder.LocalProjects)));

        return locations;
    }

    public async IAsyncEnumerable<IProjectInfo> GetRecentlyUsedProjectsAsync(
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        var state = Ioc.Default.CreateAsyncScope().ServiceProvider.GetRequiredService<PersistentState>();
        await using (state.ConfigureAwait(false))
        {
            foreach (var item in state.RecentlyUsedProjects)
            {
                if (cancellationToken.IsCancellationRequested)
                {
                    break;
                }

                var projectInfo = await this.projectsSource.LoadProjectInfoAsync(item.Location!).ConfigureAwait(false);
                if (projectInfo != null)
                {
                    projectInfo.LastUsedOn = item.LastUsedOn;
                    yield return projectInfo;
                }
                else
                {
                    try
                    {
                        _ = state.RecentlyUsedProjects!.Remove(item);
                    }
                    catch (Exception ex)
                    {
                        Debug.WriteLine($"Failed to remove entry from the most recently used projects: {ex.Message}");
                    }
                }
            }
        }
    }

    public async Task<KnownLocation[]> GetKnownLocationsAsync()
        => await this.lazyLocations.Value.ConfigureAwait(false);

    public bool CanCreateProject(string projectName, string atLocationPath)
        => this.projectsSource.CanCreateProject(projectName, atLocationPath);

    private static async Task TryUpdateRecentUsageAsync(string location)
    {
        try
        {
            var state = Ioc.Default.CreateAsyncScope().ServiceProvider.GetRequiredService<PersistentState>();
            await using (state.ConfigureAwait(false))
            {
                var recentProjectEntry = state.RecentlyUsedProjects.ToList()
                    .SingleOrDefault(
                        p => string.Equals(p.Location, location, StringComparison.Ordinal),
                        new RecentlyUsedProject(0, location));
                if (recentProjectEntry.Id != 0)
                {
                    recentProjectEntry.LastUsedOn = DateTime.Now;
                    _ = state.RecentlyUsedProjects!.Update(recentProjectEntry);
                }
                else
                {
                    state.ProjectBrowserState.RecentProjects.Add(recentProjectEntry);
                }

                _ = await state.SaveChangesAsync().ConfigureAwait(false);
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to update entry from the most recently used projects: {ex.Message}");
        }
    }

    private static async Task TryUpdateLastSaveLocation(string location)
    {
        try
        {
            var state = Ioc.Default.CreateAsyncScope().ServiceProvider.GetRequiredService<PersistentState>();
            await using (state.ConfigureAwait(false))
            {
                state.ProjectBrowserState.LastSaveLocation = location;
                _ = await state.SaveChangesAsync().ConfigureAwait(false);
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to update entry from the most recently used projects: {ex.Message}");
        }
    }

#pragma warning disable SYSLIB1054 // Use 'LibraryImportAttribute' instead of 'DllImportAttribute' to generate P/Invoke marshalling code at compile time
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, PreserveSig = false)]
    private static extern string SHGetKnownFolderPath(
        [MarshalAs(UnmanagedType.LPStruct)] Guid refToGuid,
        uint dwFlags,
        nint hToken = default);
#pragma warning restore SYSLIB1054 // Use 'LibraryImportAttribute' instead of 'DllImportAttribute' to generate P/Invoke marshalling code at compile time

    private async Task<KnownLocation[]> InitializeKnownLocationsAsync()
    {
        List<KnownLocation> locations = [];

        foreach (var locationKey in Enum.GetValues<KnownLocations>())
        {
            var location = await this.GetLocationForKeyAsync(locationKey).ConfigureAwait(false);
            if (location != null)
            {
                locations.Add(location);
            }
        }

        return [.. locations];
    }

    private async Task<KnownLocation?> GetLocationForKeyAsync(KnownLocations locationKey)
    {
        return locationKey switch
        {
            KnownLocations.RecentProjects => new KnownLocation(
                locationKey,
                "Recent Projects".TryGetLocalizedMine(),
                string.Empty,
                this.localStorage,
                this),

            KnownLocations.ThisComputer => new KnownLocation(
                locationKey,
                "This Computer".TryGetLocalizedMine(),
                string.Empty,
                this.localStorage,
                this),

            KnownLocations.OneDrive => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    SHGetKnownFolderPath(new Guid("A52BBA46-E9E1-435f-B3D9-28DAA648C0F6"), 0))
                .ConfigureAwait(false),

            KnownLocations.Downloads => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    SHGetKnownFolderPath(new Guid("374DE290-123F-4565-9164-39C4925E467B"), 0))
                .ConfigureAwait(false),

            KnownLocations.Documents => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments))
                .ConfigureAwait(false),

            KnownLocations.Desktop => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    Environment.GetFolderPath(Environment.SpecialFolder.Desktop))
                .ConfigureAwait(false),
            _ => throw new InvalidEnumArgumentException(nameof(locationKey), (int)locationKey, typeof(KnownLocations)),
        };

        async Task<KnownLocation?> LocationFromLocalFolderPathAsync(KnownLocations key, string path)
        {
            var folder = await this.localStorage.GetFolderFromPathAsync(path).ConfigureAwait(false);
            return folder != null
                ? new KnownLocation(key, folder.Name, folder.Location, this.localStorage, this)
                : null;
        }
    }
}
