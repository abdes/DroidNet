// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.Demo.Model;

public class ProjectAdapter(Project item) : TreeItemAdapter, ITreeItem<Project>
{
    public override string Label
        => this.AttachedObject.Name;

    public Project AttachedObject => item;

    protected override async Task LoadChildren()
    {
        foreach (var scene in this.AttachedObject.Scenes)
        {
            this.AddChildInternal(
                new SceneAdapter(scene)
                {
                    IsExpanded = true,
                });
        }

        await Task.CompletedTask.ConfigureAwait(true);
    }
}
