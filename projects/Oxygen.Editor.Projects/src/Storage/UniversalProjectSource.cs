// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Storage;

using Oxygen.Editor.Projects;
using Oxygen.Editor.Storage;

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

    public Task LoadProjectScenesAsync(IProject project)
        => localSource.LoadProjectScenesAsync(project);

    public Task<Scene?> LoadSceneAsync(string sceneName, IProject project)
        => localSource.LoadSceneAsync(sceneName, project);

    public IAsyncEnumerable<IFolder> LoadFoldersAsync(string location)
        => localSource.LoadFoldersAsync(location);

    public IStorageProvider GetStorageProvider() => localSource.GetStorageProvider();
}
