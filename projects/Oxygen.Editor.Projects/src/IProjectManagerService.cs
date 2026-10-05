// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Editor.World;

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

    /// <summary>Atomically saves a candidate only while the persisted project still matches the accepted configuration.</summary>
    /// <param name="projectInfo">The candidate project configuration.</param>
    /// <param name="expected">The configuration accepted by the workspace before the edit.</param>
    /// <param name="cancellationToken">Cancels before replacement.</param>
    /// <returns>Completion of the committed save; conflicts and write failures are propagated.</returns>
    public Task SaveProjectInfoAsync(IProjectInfo projectInfo, IProjectInfo expected, CancellationToken cancellationToken);

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
    /// Loads the entities of a scene asynchronously.
    /// </summary>
    /// <param name="scene">The scene whose entities are to be loaded.</param>
    /// <returns>
    /// A task that represents the asynchronous operation. The task result contains the loaded scene if successful; otherwise, <see langword="null"/>.
    /// </returns>
    public Task<Scene?> LoadSceneAsync(Scene scene);

    /// <summary>Validates a source without retaining another editable graph or changing the accepted scene.</summary>
    /// <param name="scene">The scene metadata whose source is required.</param>
    /// <param name="cancellationToken">Cancels staging without changing authoring or the save baseline.</param>
    /// <returns>Owned staged data, or null when the source is invalid or unavailable.</returns>
    public Task<SceneLoadSnapshot?> StageSceneLoadAsync(Scene scene, CancellationToken cancellationToken = default);

    /// <summary>Installs staged data after the document owner approves retiring its previous graph.</summary>
    /// <param name="snapshot">The still-current staged source owned by this service.</param>
    /// <returns>The new editable graph.</returns>
    public Scene AcceptSceneLoad(SceneLoadSnapshot snapshot);

    /// <summary>Replaces a retired editable graph with scene metadata and clears its active identity.</summary>
    /// <param name="scene">The editable graph whose document lifetime ended.</param>
    public void RetireScene(Scene scene);

    /// <summary>Gets the persisted source identity last acknowledged for a scene.</summary>
    /// <param name="scene">The scene whose saved source is required.</param>
    /// <returns>The saved source, or null if this service has not loaded or saved the scene.</returns>
    public SceneSourceVersion? GetSceneSourceVersion(Scene scene);

    /// <summary>Reads a possible reload without replacing the model or adopting a new save baseline.</summary>
    /// <param name="scene">The open scene whose source should be read.</param>
    /// <param name="cancellationToken">Cancels the read before accepting a replacement.</param>
    /// <returns>An owned replacement, or null when loading or identity validation fails.</returns>
    public Task<SceneReloadSnapshot?> ReadSceneForReloadAsync(Scene scene, CancellationToken cancellationToken = default);

    /// <summary>Installs a reload after the caller confirms discard and checks the document's revision and lifetime.</summary>
    /// <param name="snapshot">The owned read from this service for the still-current scene.</param>
    /// <returns>The replacement scene with the disk baseline adopted.</returns>
    public Scene AcceptSceneReload(SceneReloadSnapshot snapshot);

    /// <summary>
    /// Creates a new scene in the current project asynchronously.
    /// </summary>
    /// <param name="sceneName">The name of the new scene.</param>
    /// <returns>
    /// A task that represents the asynchronous operation. The task result contains the newly created scene if created successfully; otherwise, <see langword="null"/>.
    /// </returns>
    public Task<Scene?> CreateSceneAsync(string sceneName);

    /// <summary>
    /// Saves a scene to storage asynchronously.
    /// </summary>
    /// <param name="scene">The scene to save.</param>
    /// <returns>
    /// A task that represents the asynchronous operation. The task result is <see langword="true"/> if the scene is saved successfully; otherwise, <see langword="false"/>.
    /// </returns>
    public Task<bool> SaveSceneAsync(Scene scene);

    /// <summary>Persists a coherent scene snapshot without reading the live authoring model.</summary>
    /// <param name="snapshot">The immutable scene snapshot.</param>
    /// <returns>Whether the snapshot was persisted successfully.</returns>
    public Task<bool> SaveSceneSnapshotAsync(World.Serialization.SceneSaveSnapshot snapshot);

    /// <summary>Creates a scene snapshot at a new destination without overwriting any existing source.</summary>
    /// <param name="snapshot">The new asset identity, name and complete serialized content.</param>
    /// <returns>Whether the snapshot was created successfully.</returns>
    public Task<bool> CreateSceneSnapshotAsync(World.Serialization.SceneSaveSnapshot snapshot);

    /// <summary>
    /// Gets the current project's storage provider.
    /// </summary>
    /// <returns>
    /// An object implementing the <see cref="IStorageProvider"/> interface, which provides methods for managing storage items in the storage system.
    /// </returns>
    public IStorageProvider GetCurrentProjectStorageProvider();
}
