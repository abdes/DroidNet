// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

public class Scene(string name, Project project) : NamedItem(name)
{
    public Project Project => project;

    public IList<Entity> Entities { get; set; } = [];
}
