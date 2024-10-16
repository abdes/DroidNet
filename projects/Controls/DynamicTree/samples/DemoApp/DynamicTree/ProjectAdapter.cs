// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.Demo.Model;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for the <see cref="Project" /> model class.
/// </summary>
/// <param name="project">The <see cref="Entity" /> object to wrap as a <see cref="ITreeItem" />.</param>
public partial class ProjectAdapter(Project project) : TreeItemAdapter, ITreeItem<Project>
{
    public override string Label
        => this.AttachedObject.Name;

    public Project AttachedObject => project;

    protected override int GetChildrenCount() => this.AttachedObject.Scenes.Count;

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
