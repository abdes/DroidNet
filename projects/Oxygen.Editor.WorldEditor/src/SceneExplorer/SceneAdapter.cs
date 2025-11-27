// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Core;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
///     A <see cref="DynamicTree" /> item adapter for the <see cref="Scene" /> model class.
/// </summary>
/// <param name="scene">The <see cref="SceneNode" /> object to wrap as a <see cref="ITreeItem" />.</param>
public partial class SceneAdapter(Scene scene) : TreeItemAdapter, ITreeItem<Scene>
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
    public Scene AttachedObject => scene;

    /// <inheritdoc />
    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => scene.RootNodes.Count;

    /// <inheritdoc />
    protected override async Task LoadChildren()
    {
        this.ClearChildren();

        foreach (var entity in this.AttachedObject.RootNodes)
        {
            this.AddChildInternal(
                new SceneNodeAdapter(entity) { IsExpanded = false });
        }

        await Task.CompletedTask.ConfigureAwait(true);
    }
}
