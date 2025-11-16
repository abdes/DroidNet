// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// Specifies a service for managing and retrieving project usage data.
/// </summary>
/// <remarks>
/// Implementations of this service are intended to be used as a `Singleton` inside the application.
/// When such implementation is caching the data, it is important to ensure that the cache remains
/// coherent at all times or gets invalidated automatically without requiring an API call to do so.
/// </remarks>
public interface IProjectUsageService
{
    /// <summary>
    /// Determines whether there are any projects that have been used recently.
    /// </summary>
    /// <returns>
    /// A <see cref="Task{TResult}"/> representing the asynchronous operation, with a result of
    /// <see langword="true"/> if there are recently used projects; otherwise, <see langword="false"/>.
    /// </returns>
    public Task<bool> HasRecentlyUsedProjectsAsync();

    /// <summary>
    /// Retrieves a list of the most recently used projects.
    /// </summary>
    /// <param name="sizeLimit">The maximum number of items to retrieve; default is <c>10</c>.</param>
    /// <returns>
    /// A <see cref="Task{TResult}"/> representing the asynchronous operation, with a result of a
    /// list of <see cref="ProjectUsage"/> objects. When no recently used projects are found, the
    /// list will be empty.
    /// </returns>
    public Task<IList<ProjectUsage>> GetMostRecentlyUsedProjectsAsync(uint sizeLimit = 10);

    /// <summary>
    /// Retrieves the usage data for a specific project identified by its name and location.
    /// </summary>
    /// <param name="name">The name of the project.</param>
    /// <param name="location">The location of the project.</param>
    /// <returns>
    /// A <see cref="Task{TResult}"/> representing the asynchronous operation, with a result of the
    /// <see cref="ProjectUsage"/> object if found; otherwise, <see langword="null"/>.
    /// </returns>
    public Task<ProjectUsage?> GetProjectUsageAsync(string name, string location);

    /// <summary>
    /// Updates the content browser state for a specific project identified by its name and location.
    /// </summary>
    /// <param name="name">The name of the project.</param>
    /// <param name="location">The location of the project.</param>
    /// <param name="contentBrowserState">The new content browser state.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task UpdateContentBrowserStateAsync(string name, string location, string contentBrowserState);

    /// <summary>
    /// Updates the last opened scene for a specific project identified by its name and location.
    /// </summary>
    /// <param name="name">The name of the project.</param>
    /// <param name="location">The location of the project.</param>
    /// <param name="lastOpenedScene">The new last opened scene.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task UpdateLastOpenedSceneAsync(string name, string location, string lastOpenedScene);

    /// <summary>
    /// Updates the name and location for a specific project identified by its old name and old location.
    /// </summary>
    /// <param name="oldName">The old name of the project.</param>
    /// <param name="oldLocation">The old location of the project.</param>
    /// <param name="newName">The new name of the project.</param>
    /// <param name="newLocation">The new location of the project; if <see langword="null"/>, the location remains unchanged.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task UpdateProjectNameAndLocationAsync(string oldName, string oldLocation, string newName, string? newLocation = null);

    /// <summary>
    /// Updates the usage data for a specific project identified by its name and location, including the last used date and incrementing the usage count.
    /// </summary>
    /// <param name="name">The name of the project.</param>
    /// <param name="location">The location of the project.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task UpdateProjectUsageAsync(string name, string location);

    /// <summary>
    /// Deletes the usage data for a specific project identified by its <paramref name="name"/> and <paramref name="location"/>.
    /// </summary>
    /// <param name="name">The name of the project.</param>
    /// <param name="location">The location of the project.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task DeleteProjectUsageAsync(string name, string location);
}
