// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Controls;
using Oxygen.Editor.Core;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
///     A <see cref="DynamicTree" /> item adapter for the <see cref="SceneNode" /> model class.
/// </summary>
public class SceneNodeAdapter : TreeItemAdapter, ITreeItem<SceneNode>
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneNodeAdapter" /> class.
    ///     A <see cref="DynamicTree" /> item adapter for the <see cref="SceneNode" /> model class.
    /// </summary>
    /// <param name="sceneNode">The <see cref="SceneNode" /> object to wrap as a <see cref="ITreeItem" />.</param>
    public SceneNodeAdapter(SceneNode sceneNode)
    {
        this.AttachedObject = sceneNode;
        this.AttachedObject.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName?.Equals(nameof(SceneNode.Name), StringComparison.Ordinal) == true)
            {
                this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.Label)));
            }
        };
    }

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
    public SceneNode AttachedObject { get; }

    /// <inheritdoc />
    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => this.AttachedObject.Children.Count;

    /// <inheritdoc />
    protected override async Task LoadChildren()
    {
        this.ClearChildren();
        foreach (var child in this.AttachedObject.Children)
        {
            this.AddChildInternal(new SceneNodeAdapter(child));
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }
}
