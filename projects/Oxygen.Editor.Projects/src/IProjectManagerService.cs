// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using Oxygen.Editor.Projects.Utils;

public interface IProjectManagerService
{
    IProject? CurrentProject { get; }

    CategoryJsonConverter CategoryJsonConverter { get; }

    Task<IProjectInfo?> LoadProjectInfoAsync(string projectFolderPath);

    Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo);

    Task<bool> LoadProjectAsync(IProjectInfo projectInfo);

    Task<bool> LoadProjectScenesAsync(Project project);

    Task<bool> LoadSceneEntitiesAsync(Scene scene);
}
