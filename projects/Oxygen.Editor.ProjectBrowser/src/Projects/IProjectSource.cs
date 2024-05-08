// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

using Oxygen.Editor.Storage;

/// <summary>
/// The projects source can be used to enumerate projects available from a certain
/// repository.
/// </summary>
/// <para>
/// A projects source can be local or remote. The specific implementation knows how
/// to retrieve the project info for a specific project.
/// </para>
/// <para>
/// For example, a local source would use the local filesystem to retrieve the
/// project info. A remote source would use a specific mechanism to download the
/// project from the remote location, unpack it, and make it available for use in
/// the application.
/// </para>
public interface IProjectSource
{
    string[] CommonProjectLocations { get; }

    Task<IProjectInfo?> LoadProjectInfoAsync(string fullPath);

    Task<bool> MakeProjectAvailable(IProjectInfo projectInfo);

    Task<IFolder?> CreateNewProjectFolder(string projectName, string atLocationPath);

    bool CanCreateProject(string projectName, string atLocationPath);

    Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo);
}
