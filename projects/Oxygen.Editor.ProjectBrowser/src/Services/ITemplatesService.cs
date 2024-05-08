// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Services;

using Oxygen.Editor.ProjectBrowser.Templates;

public interface ITemplatesService
{
    IList<ProjectCategory> GetProjectCategories();

    ProjectCategory? GetProjectCategoryById(string id);

    IObservable<ITemplateInfo> GetLocalTemplates();

    bool HasRecentlyUsedTemplates();

    IObservable<ITemplateInfo> GetRecentlyUsedTemplates();
}
