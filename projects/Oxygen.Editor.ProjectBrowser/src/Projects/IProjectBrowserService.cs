// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ProjectBrowser.Projects;

/// <summary>
/// Defines as service API for browsing, creating, and managing projects in the Oxygen Editor.
/// </summary>
public interface IProjectBrowserService
{
    /// <summary>
    /// Gets an asynchronous enumerable of recently used projects.
    /// </summary>
    /// <param name="cancellationToken">A token to cancel the enumeration.</param>
    /// <returns>An async enumerable of project information for recently used projects.</returns>
    public IAsyncEnumerable<IProjectInfo> GetRecentlyUsedProjectsAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Validates whether a project can be created with the specified name at the given location.
    /// </summary>
    /// <param name="projectName">The name of the project to create.</param>
    /// <param name="atLocationPath">The target directory path where the project should be created.</param>
    /// <returns>
    /// <see langword="true"/> if the project can be created; <see langword="false"/> otherwise.
    /// A project can be created if the containing folder exists, and the project folder doesn't exist, or exists but is empty.
    /// </returns>
    public Task<bool> CanCreateProjectAsync(string projectName, string atLocationPath);

    /// <summary>
    /// Gets a list of quick save locations for projects.
    /// </summary>
    /// <returns>A list of quick save locations including the recently used location, personal projects, and local projects.</returns>
    public IList<QuickSaveLocation> GetQuickSaveLocations();

    /// <summary>
    /// Gets an array of known locations where projects can be stored.
    /// </summary>
    /// <returns>
    /// An array of <see cref="KnownLocation"/> objects representing standard locations like Recent Projects,
    /// This Computer, OneDrive, Downloads, Documents, and Desktop.
    /// </returns>
    public Task<KnownLocation[]> GetKnownLocationsAsync();

    /// <summary>
    /// Gets the project browser settings.
    /// </summary>
    /// <returns>The project browser settings.</returns>
    public Task<ProjectBrowserSettings> GetSettingsAsync();

    /// <summary>
    /// Saves the project browser settings.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task SaveSettingsAsync();
}
