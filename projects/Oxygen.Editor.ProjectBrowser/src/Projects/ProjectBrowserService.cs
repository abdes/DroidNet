// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using DroidNet.Resources.Generator.Localized_a870a544;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.Data;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;
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
public partial class ProjectBrowserService : IProjectBrowserService
{
    private readonly ILogger logger;

    private readonly IOxygenPathFinder finder;
    private readonly IProjectManagerService projectManager;
    private readonly NativeStorageProvider localStorage;

    private readonly Lazy<Task<KnownLocation[]>> lazyLocations;
    private readonly IProjectUsageService projectUsage;
    private readonly ITemplateUsageService templateUsage;
    private readonly Lazy<Task<ProjectBrowserSettings>> lazySettings;

    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectBrowserService"/> class.
    /// </summary>
    /// <param name="projectUsage">Service for reading and updating project usage data.</param>
    /// <param name="templateUsage">Service for recording template usage metrics.</param>
    /// <param name="projectManager">Service for managing project operations.</param>
    /// <param name="finder">Service for locating system paths.</param>
    /// <param name="localStorage">Provider for local storage operations. Must be a <see cref="NativeStorageProvider"/>.</param>
    /// <param name="settingsManager">Manager for handling project browser settings.</param>
    /// <param name="loggerFactory">The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger cannot be obtained, a <see cref="NullLogger" /> is used silently.</param>
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
        ITemplateUsageService templateUsage,
        IProjectManagerService projectManager,
        IOxygenPathFinder finder,
        IStorageProvider localStorage,
        IEditorSettingsManager settingsManager,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<NewProjectViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<NewProjectViewModel>();
        this.projectUsage = projectUsage;
        this.templateUsage = templateUsage;
        this.projectManager = projectManager;
        this.finder = finder;

        this.lazySettings = new Lazy<Task<ProjectBrowserSettings>>(
            async () =>
            {
                var settings = new ProjectBrowserSettings(settingsManager);
                await settings.LoadAsync().ConfigureAwait(true);
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
        if (!await this.localStorage.FolderExistsAsync(atLocationPath).ConfigureAwait(true))
        {
            return false;
        }

        // Project folder should not exist, but if it does, it should be empty
        var projectFolderPath = this.localStorage.NormalizeRelativeTo(atLocationPath, projectName);
        var projectFolder = await this.localStorage.GetFolderFromPathAsync(projectFolderPath).ConfigureAwait(true);
        return !await projectFolder.ExistsAsync().ConfigureAwait(true) ||
               !await projectFolder.HasItemsAsync().ConfigureAwait(true);
    }

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Project creation reports errors as boolean results and logs details for diagnostics.")]
    public async Task<bool> NewProjectFromTemplate(
        ITemplateInfo templateInfo,
        string projectName,
        string atLocationPath)
    {
        Debug.WriteLine($"New project from template: {templateInfo.Category.Name}/{templateInfo.Name} with name `{projectName}` in location `{atLocationPath}`");

        if (!await this.CanCreateProjectAsync(projectName, atLocationPath).ConfigureAwait(true))
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
                .ConfigureAwait(true);
            await projectFolder.CreateAsync().ConfigureAwait(true);

            // Copy all content from template to the new project folder
            CopyTemplateAssetsToProject();

            // After copying the template assets, patch Project.oxy at the project root (if present)
            UpdateProjectManifest(projectFolder.Location, projectName);

            // Load the project info, update it, and save it
            var projectInfo = await this.projectManager.LoadProjectInfoAsync(projectFolder.Location).ConfigureAwait(true);
            if (projectInfo != null)
            {
                projectInfo.Name = projectName;
                if (await this.projectManager.SaveProjectInfoAsync(projectInfo).ConfigureAwait(true))
                {
                    return await this.FinalizeProjectCreationAsync(projectInfo, templateInfo.Location).ConfigureAwait(true);
                }
            }

            RemoveFailedProject(projectFolder);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Project creation failed: {ex.Message}");
        }

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

        void UpdateProjectManifest(string projectFolderPath, string newName)
        {
            try
            {
                var projectOxyPath = Path.Combine(projectFolderPath, "Project.oxy");
                if (!File.Exists(projectOxyPath))
                {
                    return;
                }

                // Read JSON and update Id and Name
                var text = File.ReadAllText(projectOxyPath);
                try
                {
                    // Use System.Text.Json to parse and modify
                    var node = System.Text.Json.Nodes.JsonNode.Parse(text);
                    if (node is null)
                    {
                        return;
                    }

                    // Set Name
                    node["Name"] = newName;

                    // Set Id to a new GUID
                    node["Id"] = System.Guid.NewGuid().ToString();

                    // Write back with indentation
                    var options = new System.Text.Json.JsonSerializerOptions { WriteIndented = true };
                    var serialized = node.ToJsonString(options);
                    File.WriteAllText(projectOxyPath, serialized);
                }
                catch (System.Text.Json.JsonException)
                {
                    // If parsing fails, leave the file as-is (do not crash project creation)
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Failed to patch Project.oxy: {ex.Message}");
            }
        }

        void RemoveFailedProject(IFolder? storageLocation)
        {
            if (storageLocation == null)
            {
                return;
            }

            try
            {
                Directory.Delete(storageLocation.Location, recursive: true);
            }
            catch (Exception ex)
            {
                this.LogFailedProjectCleanupError(storageLocation.Location, ex);
                throw;
            }
        }
    }

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Project opening should not crash the UI; errors are logged and surfaced via the return value.")]
    public async Task<bool> OpenProjectAsync(IProjectInfo projectInfo)
    {
        try
        {
            var success = await this.projectManager.LoadProjectAsync(projectInfo).ConfigureAwait(true);
            if (success)
            {
                // Update the recently used project entry for the project being saved
                await this.projectUsage.UpdateProjectUsageAsync(projectInfo.Name, projectInfo.Location!).ConfigureAwait(true);
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
        var projectInfo = await this.projectManager.LoadProjectInfoAsync(location!).ConfigureAwait(true);
        return projectInfo is not null
            && await this.OpenProjectAsync(projectInfo).ConfigureAwait(true);
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
        foreach (var item in await this.projectUsage.GetMostRecentlyUsedProjectsAsync().ConfigureAwait(true))
        {
            if (cancellationToken.IsCancellationRequested)
            {
                break;
            }

            var projectInfo = await this.projectManager.LoadProjectInfoAsync(item.Location!).ConfigureAwait(true);
            if (projectInfo != null)
            {
                projectInfo.LastUsedOn = item.LastUsedOn;
                yield return projectInfo;
            }
            else
            {
                await this.projectUsage.DeleteProjectUsageAsync(item.Name, item.Location).ConfigureAwait(true);
            }
        }
    }

    /// <inheritdoc/>
    public async Task<KnownLocation[]> GetKnownLocationsAsync()
        => await this.lazyLocations.Value.ConfigureAwait(true);

    private async Task TryUpdateLastSaveLocation(string location)
    {
        try
        {
            this.Settings.LastSaveLocation = location;
            await this.Settings.SaveAsync().ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to update entry from the most recently used projects: {ex.Message}");
            throw;
        }
    }

    private async Task<bool> FinalizeProjectCreationAsync(IProjectInfo projectInfo, string templateLocation)
    {
        await this.projectUsage.UpdateProjectUsageAsync(projectInfo.Name, projectInfo.Location!).ConfigureAwait(true);

        await this.TryUpdateLastSaveLocation(new DirectoryInfo(projectInfo.Location!).Parent!.FullName).ConfigureAwait(true);

        var opened = await this.OpenProjectAsync(projectInfo).ConfigureAwait(true);
        if (!opened)
        {
            return false;
        }

        await this.TryUpdateTemplateUsageAsync(templateLocation).ConfigureAwait(true);
        return true;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Template usage tracking failures must not block project creation; log and continue.")]
    private async Task TryUpdateTemplateUsageAsync(string templateLocation)
    {
        try
        {
            await this.templateUsage.UpdateTemplateUsageAsync(templateLocation).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to update template usage for `{templateLocation}`: {ex.Message}");
        }
    }

    private async Task<KnownLocation[]> InitializeKnownLocationsAsync()
    {
        List<KnownLocation> locations = [];

        foreach (var locationKey in Enum.GetValues<KnownLocations>())
        {
            var location = await this.GetLocationForKeyAsync(locationKey).ConfigureAwait(true);
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
                "Recent Projects".L(),
                string.Empty,
                this.localStorage,
                this),

            KnownLocations.ThisComputer => new KnownLocation(
                locationKey,
                "This Computer".L(),
                string.Empty,
                this.localStorage,
                this),

            // OneDrive may not be available on all systems
            KnownLocations.OneDrive => this.finder.UserOneDrive is not null
                ? await LocationFromLocalFolderPathAsync(
                        locationKey,
                        this.finder.UserOneDrive)
                    .ConfigureAwait(true)
                : null,

            KnownLocations.Downloads => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    this.finder.UserDownloads)
                .ConfigureAwait(true),

            KnownLocations.Documents => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    this.finder.UserDocuments)
                .ConfigureAwait(true),

            KnownLocations.Desktop => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    this.finder.UserDesktop)
                .ConfigureAwait(true),
            _ => throw new InvalidEnumArgumentException(nameof(locationKey), (int)locationKey, typeof(KnownLocations)),
        };

        async Task<KnownLocation?> LocationFromLocalFolderPathAsync(KnownLocations key, string path)
        {
            var folder = await this.localStorage.GetFolderFromPathAsync(path).ConfigureAwait(true);
            return new KnownLocation(key, folder.Name, folder.Location, this.localStorage, this);
        }
    }

    [LoggerMessage(
        EventId = 1000,
        Level = LogLevel.Error,
        Message = "Could not cleanup after failed project creation at: {Location}")]
    private partial void LogFailedProjectCleanupError(string location, Exception ex);
}
