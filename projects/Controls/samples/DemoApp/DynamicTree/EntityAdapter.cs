// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.TreeView;

public class EntityAdapter(Entity item) : TreeItemAdapter, ITreeItem<Entity>
{
    public override bool IsRoot => false;

    public override string Label
        => this.Item.Name;

    public Entity Item => item;

    protected override List<TreeItemAdapter> PopulateChildren() => [];
}
