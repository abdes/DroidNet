// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Provides methods for managing projects within the Oxygen Editor, including loading, saving, and managing
///     project information and scenes for projects which storage is provided by the <see cref="IStorageProvider" />
///     specified by <paramref name="storage" />.
/// </summary>
/// <param name="storage">The <see cref="IStorageProvider" /> that can be used to access locally stored items.</param>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
/// <param name="atomicFiles">An optional atomic store override for this storage provider.</param>
public partial class ProjectManagerService(IStorageProvider storage, ILoggerFactory? loggerFactory = null, IAtomicFileStore? atomicFiles = null)
    : IProjectManagerService
{
    [SuppressMessage(
        "Performance",
        "CA1823:Avoid unused private fields",
        Justification = "used by generated logging methods")]
    private readonly ILogger logger = loggerFactory?.CreateLogger<ProjectManagerService>() ??
                                      NullLoggerFactory.Instance.CreateLogger<ProjectManagerService>();

    private readonly DocumentWriteCoordinator sceneWrites = new();
    private readonly System.Collections.Concurrent.ConcurrentDictionary<string, FileVersion> sceneVersions = new(StringComparer.OrdinalIgnoreCase);
    private readonly System.Collections.Concurrent.ConcurrentDictionary<string, SceneSourceVersion> sceneSources = new(StringComparer.OrdinalIgnoreCase);

    /// <inheritdoc />
    public IProject? CurrentProject { get; private set; }

    private IAtomicFileStore AtomicFiles => atomicFiles ?? storage.AtomicFiles;

    /// <inheritdoc />
    public IStorageProvider GetCurrentProjectStorageProvider() => storage;

    /// <inheritdoc />
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "all failures are logged and propagated as null return value")]
    public async Task<IProjectInfo?> LoadProjectInfoAsync(string projectFolderPath)
    {
        try
        {
            var projectFolder = await storage.GetFolderFromPathAsync(projectFolderPath).ConfigureAwait(true);
            var projectFile = await projectFolder.GetDocumentAsync(Constants.ProjectFileName).ConfigureAwait(true);
            var json = await projectFile.ReadAllTextAsync().ConfigureAwait(true);
            var projectInfo = ProjectInfo.FromJson(json);
            _ = projectInfo?.Location = projectFolderPath;

            return projectInfo;
        }
        catch (Exception ex)
        {
            this.CouldNotLoadProjectInfo(projectFolderPath, ex.Message);
        }

        return null;
    }

    /// <inheritdoc />
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "all failures are logged and propagated as false return value")]
    public async Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo)
    {
        Debug.Assert(projectInfo.Location != null, "The project location must be valid!");
        try
        {
            var json = ProjectInfo.ToJson(projectInfo);

            var documentPath = storage.NormalizeRelativeTo(projectInfo.Location, Constants.ProjectFileName);
            var document = await storage.GetDocumentFromPathAsync(documentPath).ConfigureAwait(true);

            await document.WriteAllTextAsync(json).ConfigureAwait(true);
            return true;
        }
        catch (Exception error)
        {
            this.CouldNotSaveProjectInfo(projectInfo.Location, error.Message);
        }

        return false;
    }

    /// <inheritdoc />
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "all failures are logged and propagated as false return value")]
    public async Task<bool> LoadProjectAsync(IProjectInfo projectInfo)
    {
        Debug.Assert(projectInfo.Location is not null, "should not load a project with an invalid project info");

        if (projectInfo.Location == null)
        {
            this.CouldNotLoadProject("__null__", "cannot not load project from `null` location");
            return false;
        }

        var project = new Project(projectInfo) { Name = projectInfo.Name, Id = projectInfo.Id };
        try
        {
            await this.LoadProjectScenesAsync(project).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            this.CouldNotLoadProject(projectInfo.Location, $"failed to load project scenes ({ex.Message})");
            return false;
        }

        this.CurrentProject = project;
        return true;
    }

    /// <inheritdoc />
    public async Task<Scene?> LoadSceneAsync(Scene scene)
    {
        var loadedScene = await this.LoadSceneFromStorageAsync(scene.Name, scene.Project).ConfigureAwait(true);
        return ReplaceLoadedScene(scene, loadedScene);
    }

    /// <inheritdoc/>
    public SceneSourceVersion? GetSceneSourceVersion(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        return scene.Project.ProjectInfo.Location is { } projectRoot
            ? this.sceneSources.GetValueOrDefault(SceneSourceKey(projectRoot, scene.Id))
            : null;
    }

    /// <inheritdoc/>
    public async Task<SceneReloadSnapshot?> ReadSceneForReloadAsync(Scene scene, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        cancellationToken.ThrowIfCancellationRequested();
        var read = await this.ReadSceneFromStorageAsync(scene.Name, scene.Project, scene.Id, cancellationToken).ConfigureAwait(true);
        return read is null ? null : new(this, scene, read.Scene, read.SourcePath, read.Version);
    }

    /// <inheritdoc/>
    public Scene AcceptSceneReload(SceneReloadSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (!ReferenceEquals(snapshot.Owner, this) || !snapshot.Original.Project.Scenes.Contains(snapshot.Original))
        {
            throw new InvalidOperationException("The scene read no longer belongs to the current project model.");
        }

        this.sceneVersions[snapshot.SourcePath] = snapshot.Version;
        this.sceneSources[SceneSourceKey(snapshot.Scene.Project.ProjectInfo.Location!, snapshot.Scene.Id)] = new(snapshot.SourcePath, snapshot.Version);
        return ReplaceLoadedScene(snapshot.Original, snapshot.Scene)!;
    }

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The authoring operation boundary preserves committed state and reports failures to the editor instead of terminating the command loop.")]
    public async Task<Scene?> CreateSceneAsync(string sceneName)
    {
        if (this.CurrentProject is null)
        {
            this.CouldNotCreateScene(sceneName, "No current project is loaded");
            return null;
        }

        if (string.IsNullOrWhiteSpace(sceneName))
        {
            this.CouldNotCreateScene(sceneName, "Scene name cannot be null or empty");
            return null;
        }

        // Check if scene already exists
        if (this.CurrentProject.Scenes.Any(s => string.Equals(s.Name, sceneName, StringComparison.OrdinalIgnoreCase)))
        {
            this.CouldNotCreateScene(sceneName, "A scene with this name already exists");
            return null;
        }

        try
        {
            // Create new scene object
            var newScene = new Scene(this.CurrentProject) { Name = sceneName };

            // Save the scene to storage
            if (!await this.SaveSceneAsync(newScene).ConfigureAwait(true))
            {
                return null;
            }

            // Add to project's scenes collection
            this.CurrentProject.Scenes.Add(newScene);

            return newScene;
        }
        catch (Exception ex)
        {
            this.CouldNotCreateScene(sceneName, ex.Message);
            return null;
        }
    }

    /// <inheritdoc />
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Snapshot and storage failures preserve the project-service boolean failure contract.")]
    public async Task<bool> SaveSceneAsync(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        try
        {
            var path = Path.Combine(scene.Project.ProjectInfo.Location ?? string.Empty, scene.Name + Constants.SceneFileExtension);
            return await this.sceneWrites.RunAsync(path, () => this.WriteSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene))).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            this.CouldNotSaveScene(scene.Name, exception.Message);
            return false;
        }
    }

    /// <inheritdoc/>
    public Task<bool> SaveSceneSnapshotAsync(SceneSaveSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        var path = Path.Combine(snapshot.ProjectLocation, snapshot.SceneName + Constants.SceneFileExtension);
        return this.sceneWrites.RunAsync(path, () => this.WriteSceneSnapshotAsync(snapshot));
    }

    /// <inheritdoc/>
    public Task<bool> CreateSceneSnapshotAsync(SceneSaveSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        var path = Path.Combine(snapshot.ProjectLocation, snapshot.SceneName + Constants.SceneFileExtension);
        return this.sceneWrites.RunAsync(path, () => this.WriteSceneSnapshotAsync(snapshot, createNew: true));
    }

    private static string SceneSourceKey(string projectRoot, Guid sceneId)
        => Path.Combine(Path.GetFullPath(projectRoot), sceneId.ToString("N"));

    private static Scene? ReplaceLoadedScene(Scene scene, Scene? loadedScene)
    {
        if (loadedScene is null)
        {
            return null;
        }

        // Replace the scene in the project's list
        var index = scene.Project.Scenes.IndexOf(scene);
        if (index != -1)
        {
            scene.Project.Scenes[index] = loadedScene;
        }

        // Update ActiveScene if it was pointing to the old scene
        if (ReferenceEquals(scene.Project.ActiveScene, scene))
        {
            scene.Project.ActiveScene = loadedScene;
        }

        return loadedScene;
    }

    private static async Task<DroidNet.Storage.IFolder> GetScenesFolderAsync(DroidNet.Storage.IFolder projectFolder)
    {
        var contentFolder = await projectFolder.GetFolderAsync(Constants.ContentFolderName).ConfigureAwait(true);
        if (!await contentFolder.ExistsAsync().ConfigureAwait(true))
        {
            await contentFolder.CreateAsync().ConfigureAwait(true);
        }

        var scenesFolder = await contentFolder.GetFolderAsync(Constants.ScenesFolderName).ConfigureAwait(true);
        if (!await scenesFolder.ExistsAsync().ConfigureAwait(true))
        {
            await scenesFolder.CreateAsync().ConfigureAwait(true);
        }

        return scenesFolder;
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The persistence boundary reports all storage failures to the calling authoring command.")]
    private async Task<bool> WriteSceneSnapshotAsync(SceneSaveSnapshot snapshot, bool createNew = false)
    {
        try
        {
            var projectFolder = await storage.GetFolderFromPathAsync(snapshot.ProjectLocation).ConfigureAwait(true);
            var scenesFolder = await GetScenesFolderAsync(projectFolder).ConfigureAwait(true);
            var sceneFile = await scenesFolder.GetDocumentAsync(snapshot.SceneName + Constants.SceneFileExtension).ConfigureAwait(true);
            var version = createNew ? FileVersion.Missing : this.sceneVersions.GetValueOrDefault(sceneFile.Location, FileVersion.Missing);
            var written = await this.AtomicFiles.WriteAsync(sceneFile.Location, System.Text.Encoding.UTF8.GetBytes(snapshot.Json), version).ConfigureAwait(true);
            this.sceneVersions[sceneFile.Location] = written;
            this.sceneSources[SceneSourceKey(snapshot.ProjectLocation, snapshot.SceneId)] = new(sceneFile.Location, written);
            return true;
        }
        catch (StorageWriteConflictException)
        {
            throw;
        }
        catch (Exception ex)
        {
            this.CouldNotSaveScene(snapshot.SceneName, ex.Message);
            return false;
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The authoring operation boundary preserves committed state and reports failures to the editor instead of terminating the command loop.")]
    private async Task LoadProjectScenesAsync(Project project)
    {
        Debug.Assert(project.ProjectInfo.Location is not null, "should not load scenes for an invalid project");

        var projectFolder = await storage.GetFolderFromPathAsync(project.ProjectInfo.Location).ConfigureAwait(true);
        var scenesFolder = await GetScenesFolderAsync(projectFolder).ConfigureAwait(true);
        if (!await scenesFolder.ExistsAsync().ConfigureAwait(true))
        {
            return;
        }

        var serializer = new Oxygen.Editor.World.Serialization.SceneSerializer(project);
        var scenes = scenesFolder.GetDocumentsAsync()
            .Where(d => d.Name.EndsWith(Constants.SceneFileExtension, StringComparison.OrdinalIgnoreCase));
        project.Scenes.Clear();
        await foreach (var item in scenes.ConfigureAwait(true))
        {
            try
            {
                var json = await item.ReadAllTextAsync().ConfigureAwait(true);
                var stream = new System.IO.MemoryStream(System.Text.Encoding.UTF8.GetBytes(json));
                await using var streamLifetime = stream.ConfigureAwait(true);
                var scene = await serializer.DeserializeAsync(stream).ConfigureAwait(true);

                // Clear nodes to maintain lazy loading behavior (nodes are loaded in LoadSceneAsync)
                scene.RootNodes.Clear();
                project.Scenes.Add(scene);
            }
            catch (Exception ex)
            {
                this.CouldNotLoadSceneMetadata(ex, item.Location);
            }
        }
    }

    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "all failures are logged and propagated as null return value")]
    private async Task<Scene?> LoadSceneFromStorageAsync(string sceneName, IProject project)
    {
        var read = await this.ReadSceneFromStorageAsync(sceneName, project).ConfigureAwait(true);
        if (read is null)
        {
            return null;
        }

        this.sceneVersions[read.SourcePath] = read.Version;
        this.sceneSources[SceneSourceKey(project.ProjectInfo.Location!, read.Scene.Id)] = new(read.SourcePath, read.Version);
        return read.Scene;
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Scene reads report storage/serialization failures without replacing authoring or its baseline.")]
    private async Task<SceneRead?> ReadSceneFromStorageAsync(string sceneName, IProject project, Guid? expectedId = null, CancellationToken cancellationToken = default)
    {
        Debug.Assert(project.ProjectInfo.Location is not null, "should not load scenes for an invalid project");

        try
        {
            var projectFolder = await storage.GetFolderFromPathAsync(project.ProjectInfo.Location!, cancellationToken)
                .ConfigureAwait(true);
            var scenesFolder = await GetScenesFolderAsync(projectFolder).ConfigureAwait(true);
            var sceneFile = await scenesFolder.GetDocumentAsync(sceneName + Constants.SceneFileExtension, cancellationToken)
                .ConfigureAwait(true);
            if (!await sceneFile.ExistsAsync().ConfigureAwait(true))
            {
                this.CouldNotLoadScene(sceneFile.Location, "file does not exist");
                return null;
            }

            // Use SceneSerializer for high-performance deserialization
            var serializer = new Oxygen.Editor.World.Serialization.SceneSerializer(project);
            var snapshot = await this.AtomicFiles.ReadAsync(sceneFile.Location, cancellationToken).ConfigureAwait(true);
            if (!snapshot.Version.Exists)
            {
                throw new FileNotFoundException("The scene disappeared while it was being opened.", sceneFile.Location);
            }

            var stream = new System.IO.MemoryStream(snapshot.Content.ToArray());
            await using var streamLifetime = stream.ConfigureAwait(true);
            var loadedScene = await serializer.DeserializeAsync(stream).ConfigureAwait(true);
            cancellationToken.ThrowIfCancellationRequested();
            return expectedId is { } identity && (loadedScene.Id != identity
                || !string.Equals(loadedScene.Name, sceneName, StringComparison.OrdinalIgnoreCase))
                ? throw new InvalidDataException("The source identifies a different scene asset or source file and cannot replace this open document.")
                : new(loadedScene, sceneFile.Location, snapshot.Version);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            var sceneLocation = storage.NormalizeRelativeTo(
                project.ProjectInfo.Location,
                $"{Constants.ContentFolderName}/{Constants.ScenesFolderName}/{sceneName}{Constants.SceneFileExtension}");
            this.CouldNotLoadScene(sceneLocation, ex.Message);
            return null;
        }
    }

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load project info from `{location}`; {error}")]
    partial void CouldNotSaveProjectInfo(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load project info from `{location}`; {error}")]
    partial void CouldNotLoadProjectInfo(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load project info from `{location}`; {error}")]
    partial void CouldNotLoadProject(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load scene from `{location}`; {error}")]
    partial void CouldNotLoadScene(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load scene from `{location}`; {error}")]
    partial void CouldNotLoadSceneEntities(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not create scene `{sceneName}`; {error}")]
    partial void CouldNotCreateScene(string sceneName, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not save scene `{sceneName}`; {error}")]
    partial void CouldNotSaveScene(string sceneName, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Failed to load scene metadata from {ScenePath}")]
    partial void CouldNotLoadSceneMetadata(Exception ex, string ScenePath);

    private sealed record SceneRead(Scene Scene, string SourcePath, FileVersion Version);
}
