// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Storage;
using Oxygen.Editor.World;

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
public partial class ProjectManagerService(IStorageProvider storage, ILoggerFactory? loggerFactory = null)
    : IProjectManagerService
{
    [SuppressMessage(
        "Performance",
        "CA1823:Avoid unused private fields",
        Justification = "used by generated logging methods")]
    private readonly ILogger logger = loggerFactory?.CreateLogger<ProjectManagerService>() ??
                                      NullLoggerFactory.Instance.CreateLogger<ProjectManagerService>();

    /// <inheritdoc />
    public IProject? CurrentProject { get; private set; }

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
    public async Task<bool> LoadSceneAsync(Scene scene)
    {
        var loadedScene = await this.LoadSceneFromStorageAsync(scene.Name, scene.Project).ConfigureAwait(true);
        if (loadedScene is null)
        {
            return false;
        }

        scene.Nodes.Clear();
        foreach (var sceneNode in loadedScene.Nodes)
        {
            scene.Nodes.Add(sceneNode);
        }

        return true;
    }

    /// <inheritdoc />
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
    public async Task<bool> SaveSceneAsync(Scene scene)
    {
        if (scene.Project.ProjectInfo.Location is null)
        {
            this.CouldNotSaveScene(scene.Name, "Project location is null");
            return false;
        }

        try
        {
            // Ensure the Scenes folder exists
            var projectFolder = await storage.GetFolderFromPathAsync(scene.Project.ProjectInfo.Location).ConfigureAwait(true);
            var scenesFolder = await projectFolder.GetFolderAsync(Constants.ScenesFolderName).ConfigureAwait(true);
            if (!await scenesFolder.ExistsAsync().ConfigureAwait(true))
            {
                await scenesFolder.CreateAsync().ConfigureAwait(true);
            }

            // Create or get the scene file
            var sceneFileName = scene.Name + Constants.SceneFileExtension;
            var sceneFile = await scenesFolder.GetDocumentAsync(sceneFileName).ConfigureAwait(true);

            // Serialize and save the scene
            var sceneJson = Scene.ToJson(scene);
            await sceneFile.WriteAllTextAsync(sceneJson).ConfigureAwait(true);

            return true;
        }
        catch (Exception ex)
        {
            this.CouldNotSaveScene(scene.Name, ex.Message);
            return false;
        }
    }

    private async Task LoadProjectScenesAsync(Project project)
    {
        Debug.Assert(project.ProjectInfo.Location is not null, "should not load scenes for an invalid project");

        var projectFolder = await storage.GetFolderFromPathAsync(project.ProjectInfo.Location).ConfigureAwait(true);
        var scenesFolder = await projectFolder.GetFolderAsync(Constants.ScenesFolderName).ConfigureAwait(true);
        if (!await scenesFolder.ExistsAsync().ConfigureAwait(true))
        {
            return;
        }

        var scenes = scenesFolder.GetDocumentsAsync()
            .Where(d => d.Name.EndsWith(Constants.SceneFileExtension, StringComparison.OrdinalIgnoreCase));
        project.Scenes.Clear();
        await foreach (var item in scenes.ConfigureAwait(true))
        {
            try
            {
                var json = await item.ReadAllTextAsync().ConfigureAwait(true);
                var scene = Scene.FromJson(json, project);
                if (scene != null)
                {
                    // Clear nodes to maintain lazy loading behavior (nodes are loaded in LoadSceneAsync)
                    scene.Nodes.Clear();
                    project.Scenes.Add(scene);
                }
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
        Debug.Assert(project.ProjectInfo.Location is not null, "should not load scenes for an invalid project");

        try
        {
            var projectFolder = await storage.GetFolderFromPathAsync(project.ProjectInfo.Location!)
                .ConfigureAwait(true);
            var scenesFolder = await projectFolder.GetFolderAsync(Constants.ScenesFolderName).ConfigureAwait(true);
            var sceneFile = await scenesFolder.GetDocumentAsync(sceneName + Constants.SceneFileExtension)
                .ConfigureAwait(true);
            if (!await sceneFile.ExistsAsync().ConfigureAwait(true))
            {
                this.CouldNotLoadScene(sceneFile.Location, "file does not exist");
                return null;
            }

            // TODO: parsing json from UTF-8 ReadOnlySpan<Byte> is more efficient
            var json = await sceneFile.ReadAllTextAsync().ConfigureAwait(true);
            var loadedScene = Scene.FromJson(json, project);
            if (loadedScene is null)
            {
                this.CouldNotLoadScene(sceneFile.Location, "JSON deserialization failed");
            }

            return loadedScene;
        }
        catch (Exception ex)
        {
            var sceneLocation = storage.NormalizeRelativeTo(
                project.ProjectInfo.Location,
                $"{Constants.ScenesFolderName}/{sceneName}{Constants.SceneFileExtension}");
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
}
