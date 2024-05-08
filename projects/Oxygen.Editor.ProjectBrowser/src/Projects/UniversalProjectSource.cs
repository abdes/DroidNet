// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

using Oxygen.Editor.Storage;

/// <summary>
/// A composite source for projects, that invokes the specific implementation based
/// on the project location.
/// </summary>
public class UniversalProjectSource : IProjectSource
{
    private readonly LocalProjectsSource localSource;

    public UniversalProjectSource(LocalProjectsSource localSource)
        => this.localSource = localSource;

    public string[] CommonProjectLocations => this.localSource.CommonProjectLocations;

    public async Task<IProjectInfo?> LoadProjectInfoAsync(string fullPath)
    {
        var projectInfo = await this.localSource.LoadProjectInfoAsync(fullPath);
        if (projectInfo != null)
        {
            projectInfo.Location = fullPath;
        }

        return projectInfo;
    }

    public async Task<bool> MakeProjectAvailable(IProjectInfo projectInfo)
        => await this.localSource.MakeProjectAvailable(projectInfo);

    public async Task<IFolder?> CreateNewProjectFolder(string projectName, string atLocationPath)
        => await this.localSource.CreateNewProjectFolder(projectName, atLocationPath);

    public bool CanCreateProject(string projectName, string atLocationPath)
        => this.localSource.CanCreateProject(projectName, atLocationPath);

    public Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo)
        => this.localSource.SaveProjectInfoAsync(projectInfo);
}
