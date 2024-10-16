// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.Projects;

public interface IProjectBrowserService
{
    IAsyncEnumerable<IProjectInfo> GetRecentlyUsedProjectsAsync(CancellationToken cancellationToken = default);

    Task<bool> CanCreateProjectAsync(string projectName, string atLocationPath);

    Task<bool> NewProjectFromTemplate(ITemplateInfo templateInfo, string projectName, string atLocationPath);

    IList<QuickSaveLocation> GetQuickSaveLocations();

    Task<KnownLocation[]> GetKnownLocationsAsync();
}
