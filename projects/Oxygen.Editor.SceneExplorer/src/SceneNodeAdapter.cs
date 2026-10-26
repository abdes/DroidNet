// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneExplorer;

/// <summary>
/// Adapter that wraps a <see cref="SceneNode" /> payload for the Scene Explorer.
/// This adapter supports the "Overlay" layout concept where the visual hierarchy
/// (folders, reordering) can differ from the scene graph.
/// </summary>
public sealed partial class SceneNodeAdapter : NodeRowAdapter, ITreeItem<SceneNode>, ICanBeCloned
{
    public SceneNodeAdapter(SceneNode payload)
    {
        this.AttachedObject = payload ?? throw new ArgumentNullException(nameof(payload));
        this.AttachedObject.Metadata.PropertyChanged += this.OnPayloadPropertyChanged;
        this.InstanceSource = payload.Components.OfType<SceneInstanceComponent>().FirstOrDefault()?.Source;
        this.Annotation = this.InstanceSource?.Name;
    }

    /// <summary>
    /// Gets the wrapped scene node payload.
    /// </summary>
    public SceneNode AttachedObject { get; }

    /// <inheritdoc />
    public override NodeReference Reference => NodeReference.To(this.AttachedObject.Id);

    /// <summary>
    /// Gets the source the node instanced when the row was made, if it was an instance root then, whose row carries the
    /// link badge and the source's file name; otherwise, <see langword="null"/>. The tree makes the row again when the
    /// node gains or loses its instance.
    /// </summary>
    public AssetPath? InstanceSource { get; }

    /// <summary>Gets a value indicating whether the node is an instance root.</summary>
    public bool IsInstanceRoot => this.InstanceSource is not null;

    public override string Label
    {
        get => this.AttachedObject.Metadata.Name;
        set => throw new NotSupportedException("Scene node name is read-only presentation; rename through the document command owner.");
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

    /// <summary>
    /// Detaches this adapter from its payload model, releasing the property-change
    /// subscription so a removed or reloaded adapter does not keep the model alive.
    /// </summary>
    public void Detach() => this.AttachedObject.Metadata.PropertyChanged -= this.OnPayloadPropertyChanged;

    private void OnPayloadPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(MetadataComponent.Name), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.Label));
        }
    }
}
