// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Storage;

namespace Oxygen.Editor.Projects;

/// <summary>
/// Provides methods for managing projects within the Oxygen Editor.
/// </summary>
/// <remarks>
/// The <see cref="IProjectManagerService"/> interface defines the methods required for managing projects within the Oxygen Editor.
/// This includes methods for loading, saving, and managing projects and their scenes, as well as accessing the current project's storage provider.
/// </remarks>
public interface IProjectManagerService
{
    /// <summary>
    /// Gets the project currently loaded in the editor, or <see langword="null"/> if no project is loaded.
    /// </summary>
    public IProject? CurrentProject { get; }

    /// <summary>
    /// Loads the project information asynchronously based on the provided project folder path.
    /// </summary>
    /// <param name="projectFolderPath">The path to the project folder.</param>
    /// <returns>
    /// A task that represents the asynchronous operation. The task result contains the project information if loaded successfully; otherwise, <see langword="null"/>.
    /// </returns>
    public Task<IProjectInfo?> LoadProjectInfoAsync(string projectFolderPath);

    /// <summary>
    /// Saves the project information asynchronously.
    /// </summary>
    /// <param name="projectInfo">The project information to save.</param>
    /// <returns>
    /// A task that represents the asynchronous operation. The task result is <see langword="true"/> if the project information is saved successfully; otherwise, <see langword="false"/>.
    /// </returns>
    public Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo);

    /// <summary>
    /// Loads a project asynchronously based on the provided project information.
    /// </summary>
    /// <param name="projectInfo">The project information to load.</param>
    /// <returns>
    /// A task that represents the asynchronous operation. The task result is <see langword="true"/> if the project is loaded successfully; otherwise, <see langword="false"/>.
    /// </returns>
    public Task<bool> LoadProjectAsync(IProjectInfo projectInfo);

    /// <summary>
    /// Loads the entities of a scene asynchronously.
    /// </summary>
    /// <param name="scene">The scene whose entities are to be loaded.</param>
    /// <returns>
    /// A task that represents the asynchronous operation. The task result is <see langword="true"/> if the entities are loaded successfully; otherwise, <see langword="false"/>.
    /// </returns>
    public Task<bool> LoadSceneAsync(Scene scene);

    /// <summary>
    /// Gets the current project's storage provider.
    /// </summary>
    /// <returns>
    /// An object implementing the <see cref="IStorageProvider"/> interface, which provides methods for managing storage items in the storage system.
    /// </returns>
    public IStorageProvider GetCurrentProjectStorageProvider();
}
