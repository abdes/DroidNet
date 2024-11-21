// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Demo.Model;

namespace DroidNet.Controls.Demo.DynamicTree;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for the <see cref="Project" /> model class.
/// </summary>
/// <param name="project">The <see cref="Entity" /> object to wrap as a <see cref="ITreeItem" />.</param>
public partial class ProjectAdapter(Project project) : TreeItemAdapter(isRoot: true, isHidden: true), ITreeItem<Project>
{
    private string label = project.Name;

    /// <inheritdoc/>
    public override string Label
    {
        get => this.label;
        set
        {
            if (string.Equals(value, this.label, StringComparison.Ordinal))
            {
                return;
            }

            this.label = value;
            this.OnPropertyChanged();
        }
    }

    /// <inheritdoc/>
    public Project AttachedObject => project;

    /// <inheritdoc/>
    public override bool ValidateItemName(string name) => name.Trim().Length != 0;

    /// <inheritdoc/>
    protected override int DoGetChildrenCount() => this.AttachedObject.Scenes.Count;

    /// <inheritdoc/>
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
