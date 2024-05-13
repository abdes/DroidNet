// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser;

using DroidNet.Resources;

public class ProjectCategory
{
    public ProjectCategory(string id, string name, string description)
    {
        this.Id = id;
        this.Name = name.TryGetLocalizedMine() ?? string.Empty;
        this.Description = description.TryGetLocalizedMine() ?? string.Empty;
    }

    public string Id { get; }

    public string Name { get; }

    public string Description { get; }
}
