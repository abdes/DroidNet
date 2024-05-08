// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Services;

using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;

public interface IProjectsService
{
    IAsyncEnumerable<IProjectInfo> GetRecentlyUsedProjectsAsync(CancellationToken cancellationToken = default);

    Task<bool> NewProjectFromTemplate(ITemplateInfo templateInfo, string projectName, string atLocationPath);

    bool CanCreateProject(string projectName, string atLocationPath);

    Task<bool> LoadProjectAsync(string location);

    IList<QuickSaveLocation> GetQuickSaveLocations();
}
