// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
/// Adapter that wraps a <see cref="SceneNode" /> payload for the Scene Explorer.
/// This adapter supports the "Overlay" layout concept where the visual hierarchy
/// (folders, reordering) can differ from the scene graph.
/// </summary>
public sealed partial class SceneNodeAdapter : LayoutItemAdapter, ITreeItem<SceneNode>, ICanBeCloned
{
    public SceneNodeAdapter(SceneNode payload)
    {
        this.AttachedObject = payload ?? throw new ArgumentNullException(nameof(payload));
        this.IsExpanded = this.AttachedObject.IsExpanded;
        this.AttachedObject.PropertyChanged += this.OnPayloadPropertyChanged;
    }

    /// <summary>
    /// Gets the wrapped scene node payload.
    /// </summary>
    public SceneNode AttachedObject { get; }

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

    // Default node glyph (Cube) when not acting as a folder
    protected override string DefaultIconGlyph => "\uE7C1";

    /// <inheritdoc />
    public ITreeItem CloneSelf()
    {
        // Return a new adapter with the same payload (SceneNode).
        // The Service will handle the actual duplication of the SceneNode if needed.
        return new SceneNodeAdapter(this.AttachedObject);
    }

    private void OnPayloadPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(SceneNode.Name), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.Label));
        }
        else if (string.Equals(e.PropertyName, nameof(SceneNode.IsExpanded), StringComparison.Ordinal))
        {
            this.IsExpanded = this.AttachedObject.IsExpanded;
        }
    }
}
