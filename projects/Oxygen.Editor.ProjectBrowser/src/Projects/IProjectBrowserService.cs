// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ProjectBrowser.Templates;
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
    /// <remarks>
    /// Invalid or missing projects are automatically removed from the recent projects list during enumeration.
    /// </remarks>
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
    /// Creates a new project from the specified template.
    /// </summary>
    /// <param name="templateInfo">Information about the template to use.</param>
    /// <param name="projectName">The name of the new project.</param>
    /// <param name="atLocationPath">The target directory path where the project should be created.</param>
    /// <returns><see langword="true"/> if the project was created successfully; <see langword="false"/> otherwise.</returns>
    /// <remarks>
    /// The operation includes:
    /// <para>- Creating the project directory.</para>
    /// <para>- Copying template assets to the project folder.</para>
    /// <para>- Updating project information with the new name.</para>
    /// <para>- Updating recent project usage records.</para>
    /// If the operation fails, any created project folder will be cleaned up.
    /// </remarks>
    public Task<bool> NewProjectFromTemplate(ITemplateInfo templateInfo, string projectName, string atLocationPath);

    /// <summary>
    /// Opens an existing project using its project information.
    /// </summary>
    /// <param name="projectInfo">The project information for the project to open.</param>
    /// <returns><see langword="true"/> if the project was opened successfully; <see langword="false"/> otherwise.</returns>
    public Task<bool> OpenProjectAsync(IProjectInfo projectInfo);

    /// <summary>
    /// Opens an existing project from its location path.
    /// </summary>
    /// <param name="location">The full path to the project.</param>
    /// <returns><see langword="true"/> if the project was opened successfully; <see langword="false"/> otherwise.</returns>
    public Task<bool> OpenProjectAsync(string location);

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
