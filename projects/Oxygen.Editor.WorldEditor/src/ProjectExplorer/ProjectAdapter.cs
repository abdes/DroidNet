// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Core;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

/// <summary>
///     A <see cref="DynamicTree" /> item adapter for the <see cref="Project" /> model class.
/// </summary>
/// <param name="project">The <see cref="SceneNode" /> object to wrap as a <see cref="ITreeItem" />.</param>
public partial class ProjectAdapter(Project project) : TreeItemAdapter(true, true), ITreeItem<Project>
{
    /// <inheritdoc />
    public override string Label
    {
        get => this.AttachedObject.Name;
        set
        {
            if (string.Equals(value, this.AttachedObject.Name, StringComparison.Ordinal))
            {
                return;
            }

            this.AttachedObject.Name = value;
            this.OnPropertyChanged();
        }
    }

    /// <inheritdoc />
    public Project AttachedObject => project;

    /// <inheritdoc />
    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => project.Scenes.Count;

    /// <inheritdoc />
    protected override async Task LoadChildren()
    {
        this.ClearChildren();

        foreach (var scene in project.Scenes)
        {
            this.AddChildInternal(
                new SceneAdapter(scene) { IsExpanded = true });
        }

        await Task.CompletedTask.ConfigureAwait(true);
    }
}
