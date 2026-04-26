// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.CompilerServices;
using DroidNet.Resources.Generator.Localized_a870a544;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Core.Services;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Storage;
using Oxygen.Storage.Native;

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
/// - Handling project storage locations
/// - Tracking recently used projects
/// - Managing project browser settings.
/// </para>
/// </remarks>
public partial class ProjectBrowserService : IProjectBrowserService
{
    private readonly ILogger logger;

    private readonly IOxygenPathFinder finder;
    private readonly IProjectCreationService projectCreation;
    private readonly IRecentProjectAdapter recentProjects;
    private readonly NativeStorageProvider localStorage;
    private readonly IEditorSettingsManager settingsManager;

    private readonly Lazy<Task<KnownLocation[]>> lazyLocations;
    private readonly Lazy<Task<ProjectBrowserSettings>> lazySettings;

    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectBrowserService"/> class.
    /// </summary>
    /// <param name="projectCreation">Project creation validation service.</param>
    /// <param name="recentProjects">Recent project adapter.</param>
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
        IProjectCreationService projectCreation,
        IRecentProjectAdapter recentProjects,
        IOxygenPathFinder finder,
        IStorageProvider localStorage,
        IEditorSettingsManager settingsManager,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<ProjectBrowserService>() ?? NullLoggerFactory.Instance.CreateLogger<ProjectBrowserService>();
        this.projectCreation = projectCreation;
        this.recentProjects = recentProjects;
        this.finder = finder;
        this.settingsManager = settingsManager;

        this.lazySettings = new Lazy<Task<ProjectBrowserSettings>>(
            async () =>
            {
                var settings = new ProjectBrowserSettings();
                await settings.LoadAsync(settingsManager).ConfigureAwait(true);
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
        => await this.projectCreation.CanCreateProjectAsync(projectName, atLocationPath).ConfigureAwait(true);

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
    public async IAsyncEnumerable<RecentProjectEntry> GetRecentlyUsedProjectsAsync([EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        await foreach (var item in this.recentProjects.GetRecentProjectsAsync(cancellationToken: cancellationToken).ConfigureAwait(true))
        {
            yield return item;
        }
    }

    /// <inheritdoc/>
    public async Task RemoveRecentProjectAsync(
        string name,
        string location,
        CancellationToken cancellationToken = default)
        => await this.recentProjects.RemoveAsync(name, location, cancellationToken).ConfigureAwait(true);

    /// <inheritdoc/>
    public async Task<KnownLocation[]> GetKnownLocationsAsync()
        => await this.lazyLocations.Value.ConfigureAwait(true);

    /// <inheritdoc/>
    public async Task<ProjectBrowserSettings> GetSettingsAsync()
        => await this.lazySettings.Value.ConfigureAwait(false);

    /// <inheritdoc/>
    public async Task SaveSettingsAsync()
    {
        var settings = await this.lazySettings.Value.ConfigureAwait(false);
        await settings.SaveAsync(this.settingsManager).ConfigureAwait(false);
    }

    private async Task TryUpdateLastSaveLocation(string location)
    {
        try
        {
            this.Settings.LastSaveLocation = location;
            await this.Settings.SaveAsync(this.settingsManager).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            this.LogFailedToUpdateLastSaveLocation(ex);
            throw;
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
                "PB_Locations_RecentProjects".L(),
                string.Empty,
                this.localStorage,
                this),

            KnownLocations.ThisComputer => new KnownLocation(
                locationKey,
                "PB_Locations_ThisComputer".L(),
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

            KnownLocations.LocalProjects => await LocationFromLocalFolderPathAsync(
                    locationKey,
                    this.finder.LocalProjects)
                .ConfigureAwait(true),

            _ => throw new InvalidEnumArgumentException(nameof(locationKey), (int)locationKey, typeof(KnownLocations)),
        };

        async Task<KnownLocation?> LocationFromLocalFolderPathAsync(KnownLocations key, string path)
        {
            var folder = await this.localStorage.GetFolderFromPathAsync(path).ConfigureAwait(true);
            return new KnownLocation(key, folder.Name, folder.Location, this.localStorage, this);
        }
    }
}
