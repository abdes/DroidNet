// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser;

using DroidNet.Resources;

public class ProjectCategory(string id, string name, string description)
{
    public string Id { get; } = id;

    public string Name { get; } = name.TryGetLocalizedMine();

    public string Description { get; } = description.TryGetLocalizedMine();
}
