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
            this.CurrentProject = new Project(projectInfo);
            return await Task.FromResult(true).ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            this.CouldNotLoadProject(projectInfo.Location ?? string.Empty, ex.Message);
            return await Task.FromResult(true).ConfigureAwait(false);
        }
    }

    public async Task<bool> LoadProjectScenesAsync(IProject project)
    {
        try
        {
            // TODO: Populate the project's list of scenes
            await Task.CompletedTask.ConfigureAwait(false);

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
        try
        {
            // TODO: Load the scene from its corresponding file
            await Task.CompletedTask.ConfigureAwait(false);

            return true;
        }
        catch (Exception ex)
        {
            this.CouldNotLoadScene(scene.Project.ProjectInfo.Location ?? string.Empty, ex.Message);
            return await Task.FromResult(true).ConfigureAwait(false);
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
    partial void CouldNotLoadScene(string location, string error);
}
