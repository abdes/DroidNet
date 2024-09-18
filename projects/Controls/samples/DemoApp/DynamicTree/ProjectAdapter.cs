// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.TreeView;

public class ProjectAdapter(Project item) : TreeItemAdapter, ITreeItem<Project>
{
    public override bool IsRoot => true;

    public override string Label
        => this.Item.Name;

    public Project Item => item;

    protected override List<TreeItemAdapter> PopulateChildren()
    {
        List<TreeItemAdapter> children = [];
        children.AddRange(
            this.Item.Scenes.Select(
                child => new SceneAdapter(child)
                {
                    Level = this.Level + 1,
                    IsExpanded = false,
                }));

        return children;
    }
}
