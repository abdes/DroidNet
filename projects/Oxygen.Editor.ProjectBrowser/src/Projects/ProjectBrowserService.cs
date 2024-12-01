// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using DroidNet.Resources;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.Data;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;

namespace Oxygen.Editor.ProjectBrowser.Projects;

/* TODO: better error reporting to the caller */

/// <summary>
/// Implements the <see cref="IProjectBrowserService"/> interface to provide project management functionality
/// in the Oxygen Editor environment.
/// </summary>
/// <remarks>
/// <para>
/// The ProjectBrowserService handles project creation, browsing, and management operations while maintaining
/// state for recently used projects and known locations.
/// </para>
/// <para>
/// Key responsibilities include:
/// - Managing project templates and creation
/// - Handling project storage locations
/// - Tracking recently used projects
/// - Managing project browser settings.
/// </para>
/// </remarks>
public class ProjectBrowserService : IProjectBrowserService
{
    private readonly IOxygenPathFinder finder;
    private readonly IProjectManagerService projectManager;
    private readonly NativeStorageProvider localStorage;

    private readonly Lazy<Task<KnownLocation[]>> lazyLocations;
    private readonly IProjectUsageService projectUsage;
    private readonly Lazy<Task<ProjectBrowserSettings>> lazySettings;

    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectBrowserService"/> class.
    /// </summary>
    /// <param name="projectManager">Service for managing project operations.</param>
    /// <param name="finder">Service for locating system paths.</param>
    /// <param name="localStorage">Provider for local storage operations. Must be a <see cref="NativeStorageProvider"/>.</param>
    /// <param name="settingsManager">Manager for handling project browser settings.</param>
    /// <exception cref="ArgumentException">
    /// Thrown when <paramref name="localStorage"/> is not an instance of <see cref="NativeStorageProvider"/>.
    /// </exception>
    /// <remarks>
    /// The constructor initializes lazy-loaded components:
    /// <para>- Project browser settings.</para>
    /// <para>- Known locations for project storage.</para>
    /// </remarks>
    public ProjectBrowserService(
        IProjectUsageService projectUsage,
        IProjectManagerService projectManager,
        IOxygenPathFinder finder,
        IStorageProvider localStorage,
        ISettingsManager settingsManager)
    {
        this.projectUsage = projectUsage;
        this.projectManager = projectManager;
        this.finder = finder;

        this.lazySettings = new Lazy<Task<ProjectBrowserSettings>>(
            async () =>
            {
                var settings = new ProjectBrowserSettings(settingsManager);
                await settings.LoadAsync().ConfigureAwait(false);
                return settings;
            });

        this.localStorage = localStorage as NativeStorageProvider ?? throw new ArgumentException(
            $"{nameof(ProjectBrowserService)} requires a {nameof(NativeStorageProvider)}",
            nameof(localStorage));

        this.lazyLocations = new Lazy<Task<KnownLocation[]>>(this.InitializeKnownLocationsAsync);
    }

    /// <summary>
    /// Gets the current project browser settings.
    /// </summary>
    /// <remarks>
    /// Accesses the lazily-loaded settings instance. The settings are loaded only when first accessed.
    /// </remarks>
    private ProjectBrowserSettings Settings => this.lazySettings.Value.Result;

    /// <inheritdoc/>
    public async Task<bool> CanCreateProjectAsync(string projectName, string atLocationPath)
    {
        // Containing folder must exist
        if (!await this.localStorage.FolderExistsAsync(atLocationPath).ConfigureAwait(false))
        {
            return false;
        }

        // Project folder should not exist, but if it does, it should be empty
        var projectFolderPath = this.localStorage.NormalizeRelativeTo(atLocationPath, projectName);
        var projectFolder = await this.localStorage.GetFolderFromPathAsync(projectFolderPath).ConfigureAwait(false);
        return !await projectFolder.ExistsAsync().ConfigureAwait(false) ||
               !await projectFolder.HasItemsAsync().ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task<bool> NewProjectFromTemplate(
        ITemplateInfo templateInfo,
        string projectName,
        string atLocationPath)
    {
        Debug.WriteLine($"New project from template: {templateInfo.Category.Name}/{templateInfo.Name} with name `{projectName}` in location `{atLocationPath}`");

        if (!await this.CanCreateProjectAsync(projectName, atLocationPath).ConfigureAwait(false))
        {
            Debug.WriteLine($"Cannot create a new project with name `{projectName}` at: {atLocationPath}");
            return false;
        }

        if (string.IsNullOrEmpty(templateInfo.Location))
        {
            Debug.WriteLine($"Invalid template location: {templateInfo.Location}");
            return false;
        }

        var templateDirInfo = new DirectoryInfo(templateInfo.Location);

        IFolder? projectFolder = null;
        try
        {
            // Create project folder
            projectFolder = await this.localStorage
                .GetFolderFromPathAsync(this.localStorage.NormalizeRelativeTo(atLocationPath, projectName))
                .ConfigureAwait(false);
            await projectFolder.CreateAsync().ConfigureAwait(false);

            // Copy all content from template to the new project folder
            CopyTemplateAssetsToProject();

            // Load the project info, update it, and save it
            var projectInfo
                = await this.projectManager.LoadProjectInfoAsync(projectFolder.Location).ConfigureAwait(false);
            if (projectInfo != null)
            {
                projectInfo.Name = projectName;
                if (await this.projectManager.SaveProjectInfoAsync(projectInfo).ConfigureAwait(false))
                {
                    // Update the recently used project entry for the project being saved
                    await this.projectUsage.UpdateProjectUsageAsync(projectInfo.Name, projectInfo.Location!).ConfigureAwait(false);

                    // Update the last save location
                    await this.TryUpdateLastSaveLocation(new DirectoryInfo(projectInfo.Location!).Parent!.FullName)
                        .ConfigureAwait(false);

                    return true;
                }
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Project creation failed: {ex.Message}");
        }

        RemoveFailedProject(projectFolder);
        return false;

        void CopyTemplateAssetsToProject()
        {
            // TODO: fix this to implement recursive copying of the template using async enumeration with cancellation
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

    /// <inheritdoc/>
    public async Task<bool> OpenProjectAsync(IProjectInfo projectInfo)
    {
        try
        {
            var success = await this.projectManager.LoadProjectAsync(projectInfo).ConfigureAwait(false);
            if (success)
            {
                // Update the recently used project entry for the project being saved
                await this.projectUsage.UpdateProjectUsageAsync(projectInfo.Name, projectInfo.Location!).ConfigureAwait(false);
            }

            return success;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Open project  failed: {ex.Message}");
            return false;
        }
    }

    /// <inheritdoc/>
    public async Task<bool> OpenProjectAsync(string location)
    {
        var projectInfo = await this.projectManager.LoadProjectInfoAsync(location!).ConfigureAwait(false);
        return projectInfo is not null
            && await this.OpenProjectAsync(projectInfo).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public IList<QuickSaveLocation> GetQuickSaveLocations()
    {
        var locations = new List<QuickSaveLocation>();

        var lastSaveLocation = this.Settings.LastSaveLocation;
        if (!string.IsNullOrEmpty(lastSaveLocation))
        {
            locations.Add(new QuickSaveLocation("Recently Used", lastSaveLocation));
        }

        locations.Add(new QuickSaveLocation("Personal Projects", this.finder.PersonalProjects));
        locations.Add(new QuickSaveLocation("Local Projects", this.finder.LocalProjects));

        return locations;
    }

    /// <inheritdoc/>
    public async IAsyncEnumerable<IProjectInfo> GetRecentlyUsedProjectsAsync([EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        foreach (var item in await this.projectUsage.GetMostRecentlyUsedProjectsAsync().ConfigureAwait(false))
        {
            if (cancellationToken.IsCancellationRequested)
            {
                break;
            }

            var projectInfo = await this.projectManager.LoadProjectInfoAsync(item.Location!).ConfigureAwait(false);
            if (projectInfo != null)
            {
                projectInfo.LastUsedOn = item.LastUsedOn;
                yield return projectInfo;
            }
            else
            {
                await this.projectUsage.DeleteProjectUsageAsync(item.Name, item.Location).ConfigureAwait(false);
            }
        }
    }

    /// <inheritdoc/>
    public async Task<KnownLocation[]> GetKnownLocationsAsync()
        => await this.lazyLocations.Value.ConfigureAwait(false);

    private async Task TryUpdateLastSaveLocation(string location)
    {
        try
        {
            this.Settings.LastSaveLocation = location;
            await this.Settings.SaveAsync().ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to update entry from the most recently used projects: {ex.Message}");
            throw;
        }
    }

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
                "Recent Projects".GetLocalizedMine(),
                string.Empty,
                this.localStorage,
                this),

            KnownLocations.ThisComputer => new KnownLocation(
                locationKey,
                "This Computer".GetLocalizedMine(),
                string.Empty,
                this.localStorage,
                this),

            KnownLocations.OneDrive => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    this.finder.UserOneDrive)
                .ConfigureAwait(false),

            KnownLocations.Downloads => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    this.finder.UserDownloads)
                .ConfigureAwait(false),

            KnownLocations.Documents => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    this.finder.UserDocuments)
                .ConfigureAwait(false),

            KnownLocations.Desktop => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    this.finder.UserDesktop)
                .ConfigureAwait(false),
            _ => throw new InvalidEnumArgumentException(nameof(locationKey), (int)locationKey, typeof(KnownLocations)),
        };

        async Task<KnownLocation?> LocationFromLocalFolderPathAsync(KnownLocations key, string path)
        {
            var folder = await this.localStorage.GetFolderFromPathAsync(path).ConfigureAwait(false);
            return new KnownLocation(key, folder.Name, folder.Location, this.localStorage, this);
        }
    }
}
