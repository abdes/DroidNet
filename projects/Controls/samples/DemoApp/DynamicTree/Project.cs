// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

public class Project(string name) : NamedItem(name)
{
    public IList<Scene> Scenes { get; set; } = [];
}
