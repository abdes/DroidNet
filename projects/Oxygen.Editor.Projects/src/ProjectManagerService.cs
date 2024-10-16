// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Oxygen.Editor.Projects.Config;
using Oxygen.Editor.Projects.Storage;
using Oxygen.Editor.Projects.Utils;

/// <summary>
/// Project management service.
/// </summary>
public partial class ProjectManagerService : IProjectManagerService
{
    private readonly ILogger<ProjectManagerService> logger;

    private readonly IProjectSource projectSource;

    public ProjectManagerService(
        IProjectSource projectSource,
        IOptions<ProjectsSettings> settings,
        ILogger<ProjectManagerService> logger)
    {
        // Get rid of style warnings to convert to primary constructor.
        // We need a logger field for logging source generators.
        _ = 1;

        this.logger = logger;

        this.projectSource = projectSource;
        this.CategoryJsonConverter = new CategoryJsonConverter(settings.Value);
    }

    public CategoryJsonConverter CategoryJsonConverter { get; }

    public IProject? CurrentProject { get; private set; }

    public async Task<bool> LoadProjectAsync(IProjectInfo projectInfo)
    {
        try
        {
            this.CurrentProject = new Project(projectInfo) { Name = projectInfo.Name };
            return await Task.FromResult(true).ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            this.CouldNotLoadProject(projectInfo.Location ?? string.Empty, ex.Message);
            return await Task.FromResult(true).ConfigureAwait(false);
        }
    }

    public async Task<bool> LoadProjectScenesAsync(Project project)
    {
        try
        {
            await this.projectSource.LoadProjectScenesAsync(project).ConfigureAwait(false);
            return true;
        }
        catch (Exception ex)
        {
            this.CouldNotLoadProject(project.ProjectInfo.Location ?? string.Empty, ex.Message);
            return await Task.FromResult(true).ConfigureAwait(false);
        }
    }

    public async Task<bool> LoadSceneEntitiesAsync(Scene scene)
    {
        if (scene.Project is not { ProjectInfo.Location: not null })
        {
            this.CouldNotLoadSceneEntities(scene.Name, "null project or project location");
            return false;
        }

        try
        {
            var loadedScene = await this.projectSource.LoadSceneAsync(
                    scene.Name,
                    scene.Project.ProjectInfo.Location)
                .ConfigureAwait(false);

            if (loadedScene is null)
            {
                return false;
            }

            scene.Entities.Clear();
            foreach (var entity in loadedScene.Entities)
            {
                scene.Entities.Add(entity);
            }

            return true;
        }
        catch (Exception ex)
        {
            this.CouldNotLoadSceneEntities(scene.Project.ProjectInfo.Location, ex.Message);
            return false;
        }
    }

    public async Task<IProjectInfo?> LoadProjectInfoAsync(string projectFolderPath) =>
        await this.projectSource.LoadProjectInfoAsync(projectFolderPath).ConfigureAwait(false);

    public async Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo) =>
        await this.projectSource.SaveProjectInfoAsync(projectInfo).ConfigureAwait(false);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load project info from `{location}`; {error}")]
    partial void CouldNotLoadProject(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load scene from `{location}`; {error}")]
    partial void CouldNotLoadSceneEntities(string location, string error);
}
