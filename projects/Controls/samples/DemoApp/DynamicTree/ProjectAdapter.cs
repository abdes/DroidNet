// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.Demo.Model;
using DroidNet.Controls.DynamicTree;

public class ProjectAdapter(Project item) : TreeItemAdapter, ITreeItem<Project>
{
    public override string Label
        => this.Item.Name;

    public Project Item => item;

    protected override async Task LoadChildren()
    {
        foreach (var scene in this.Item.Scenes)
        {
            this.AddChildInternal(
                new SceneAdapter(scene)
                {
                    Level = this.Level + 1,
                    IsExpanded = true,
                });
        }

        await Task.CompletedTask.ConfigureAwait(true);
    }
}
