// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using Oxygen.Editor.Projects.Utils;
using Oxygen.Editor.Storage;

public interface IProjectManagerService
{
    IProject? CurrentProject { get; }

    CategoryJsonConverter CategoryJsonConverter { get; }

    Task<IProjectInfo?> LoadProjectInfoAsync(string projectFolderPath);

    Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo);

    Task<bool> LoadProjectAsync(IProjectInfo projectInfo);

    Task<bool> LoadProjectScenesAsync(IProject project);

    Task<bool> LoadSceneEntitiesAsync(Scene scene);

    IStorageProvider GetCurrentProjectStorageProvider();
}
