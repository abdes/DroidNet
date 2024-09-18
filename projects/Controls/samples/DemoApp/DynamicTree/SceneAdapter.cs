// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.TreeView;

public class SceneAdapter(Scene item) : TreeItemAdapter, ITreeItem<Scene>
{
    public override bool IsRoot => false;

    public override string Label
        => this.Item.Name;

    public Scene Item => item;

    protected override List<TreeItemAdapter> PopulateChildren()
    {
        List<TreeItemAdapter> children = [];
        children.AddRange(
            this.Item.Entities.Select(
                child => new EntityAdapter(child)
                {
                    Level = this.Level + 1,
                    IsExpanded = true,
                }));

        return children;
    }
}
