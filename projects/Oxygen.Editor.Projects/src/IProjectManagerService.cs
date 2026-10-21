// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;

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

    /// <summary>Atomically saves a candidate only while the persisted project still matches the accepted configuration.</summary>
    /// <param name="projectInfo">The candidate project configuration.</param>
    /// <param name="expected">The configuration accepted by the workspace before the edit.</param>
    /// <param name="cancellationToken">Cancels before replacement.</param>
    /// <returns>Completion of the committed save; conflicts and write failures are propagated.</returns>
    public Task SaveProjectInfoAsync(IProjectInfo projectInfo, IProjectInfo expected, CancellationToken cancellationToken);

    /// <summary>Saves a project's settings into its manifest and keeps the rest of the manifest as it is on disk.</summary>
    /// <param name="projectRoot">The project's folder.</param>
    /// <param name="settings">The settings to save.</param>
    /// <param name="cancellationToken">Cancels the save.</param>
    /// <returns>Completion after the manifest is written.</returns>
    public Task SaveProjectSettingsAsync(string projectRoot, ProjectSettings settings, CancellationToken cancellationToken);

    /// <summary>
    /// Loads a project asynchronously based on the provided project information.
    /// </summary>
    /// <param name="projectInfo">The project information to load.</param>
    /// <returns>
    /// A task that represents the asynchronous operation. The task result is <see langword="true"/> if the project is loaded successfully; otherwise, <see langword="false"/>.
    /// </returns>
    public Task<bool> LoadProjectAsync(IProjectInfo projectInfo);

    /// <summary>Reads project and scene metadata without replacing the active project.</summary>
    /// <param name="projectInfo">The validated project manifest.</param>
    /// <param name="cancellationToken">Cancels staging without changing the accepted project.</param>
    /// <returns>Owned project metadata, or null when staging fails.</returns>
    public Task<ProjectLoadSnapshot?> StageProjectLoadAsync(IProjectInfo projectInfo, CancellationToken cancellationToken = default);

    /// <summary>Tests whether a staged project may still replace the accepted project.</summary>
    /// <param name="snapshot">The staged project.</param>
    /// <returns>
    ///     <see langword="true"/> when the snapshot is owned by this service, not yet accepted, and the
    ///     accepted project has not changed since staging began.
    /// </returns>
    public bool IsProjectLoadCurrent(ProjectLoadSnapshot snapshot);

    /// <summary>Accepts a staged project after the host's document close guards approve replacement.</summary>
    /// <param name="snapshot">The staged project owned by this service.</param>
    /// <returns>The newly active project.</returns>
    /// <exception cref="InvalidOperationException">The snapshot is foreign, already accepted, or stale.</exception>
    public IProject AcceptProjectLoad(ProjectLoadSnapshot snapshot);

    /// <summary>
    /// Gets the current project's storage provider.
    /// </summary>
    /// <returns>
    /// An object implementing the <see cref="IStorageProvider"/> interface, which provides methods for managing storage items in the storage system.
    /// </returns>
    public IStorageProvider GetCurrentProjectStorageProvider();
}
