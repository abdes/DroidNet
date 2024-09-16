// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

using Oxygen.Editor.Projects;
using Oxygen.Editor.Storage;

/// <summary>
/// A composite source for projects, that invokes the specific implementation based
/// on the project location.
/// </summary>
public class UniversalProjectSource(LocalProjectsSource localSource) : IProjectSource
{
    public string[] CommonProjectLocations => localSource.CommonProjectLocations;

    public async Task<IProjectInfo?> LoadProjectInfoAsync(string projectFolderPath)
    {
        var projectInfo = await localSource.LoadProjectInfoAsync(projectFolderPath).ConfigureAwait(true);
        if (projectInfo != null)
        {
            projectInfo.Location = projectFolderPath;
        }

        return projectInfo;
    }

    public async Task<IFolder?> CreateNewProjectFolderAsync(string projectName, string atLocationPath)
        => await localSource.CreateNewProjectFolderAsync(projectName, atLocationPath).ConfigureAwait(true);

    public bool CanCreateProject(string projectName, string atLocationPath)
        => localSource.CanCreateProject(projectName, atLocationPath);

    public Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo)
        => localSource.SaveProjectInfoAsync(projectInfo);
}
