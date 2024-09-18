// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

public class Scene(string name) : NamedItem(name)
{
    public IList<Entity> Entities { get; set; } = [];
}
