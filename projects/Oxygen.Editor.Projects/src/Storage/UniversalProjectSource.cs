// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Storage;

using Oxygen.Editor.Projects;

/// <summary>
/// A composite source for projects, that invokes the specific implementation based
/// on the project location.
/// </summary>
/// <param name="localSource">
/// A project source that can be used for locally stored projects.
/// </param>
public class UniversalProjectSource(LocalProjectsSource localSource) : IProjectSource
{
    public Task<IProjectInfo?> LoadProjectInfoAsync(string projectFolderPath)
        => localSource.LoadProjectInfoAsync(projectFolderPath);

    public Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo)
        => localSource.SaveProjectInfoAsync(projectInfo);

    public Task LoadProjectScenesAsync(Project project)
        => localSource.LoadProjectScenesAsync(project);

    public Task<Scene?> LoadSceneAsync(string sceneName, Project project)
        => localSource.LoadSceneAsync(sceneName, project);
}
