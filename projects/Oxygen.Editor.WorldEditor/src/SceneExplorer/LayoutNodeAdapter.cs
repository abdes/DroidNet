// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Controls;
using Oxygen.Editor.Core;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
/// Layout-only adapter that wraps a <see cref="SceneNodeAdapter" /> payload. Moving this adapter
/// in the layout does not mutate the underlying scene graph; the payload can be retargeted without
/// changing the scene node itself.
/// </summary>
public sealed class LayoutNodeAdapter : TreeItemAdapter, ITreeItem<SceneNodeAdapter>
{
    private SceneNodeAdapter payload;

    public LayoutNodeAdapter(SceneNodeAdapter payload)
    {
        this.payload = payload ?? throw new ArgumentNullException(nameof(payload));
        this.IsExpanded = this.payload.IsExpanded;
        this.payload.PropertyChanged += this.OnPayloadPropertyChanged;
    }

    /// <summary>
    /// Gets the wrapped scene node adapter.
    /// </summary>
    public SceneNodeAdapter AttachedObject => this.payload;

    public override string Label
    {
        get => this.payload.Label;
        set => this.payload.Label = value;
    }

    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <summary>
    /// Replaces the wrapped payload without affecting layout position.
    /// </summary>
    /// <param name="newPayload">New scene node adapter to attach.</param>
    public void AttachPayload(SceneNodeAdapter newPayload)
    {
        if (newPayload is null)
        {
            throw new ArgumentNullException(nameof(newPayload));
        }

        if (ReferenceEquals(this.payload, newPayload))
        {
            return;
        }

        this.payload.PropertyChanged -= this.OnPayloadPropertyChanged;
        this.payload = newPayload;
        this.payload.PropertyChanged += this.OnPayloadPropertyChanged;
        this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.Label)));
    }

    protected override int DoGetChildrenCount() => this.AttachedObject.AttachedObject.Children.Count;

    protected override async Task LoadChildren()
    {
        this.ClearChildren();
        foreach (var child in this.AttachedObject.AttachedObject.Children)
        {
            var childAdapter = new SceneNodeAdapter(child);
            this.AddChildInternal(new LayoutNodeAdapter(childAdapter));
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    private void OnPayloadPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        _ = sender;
        if (args.PropertyName?.Equals(nameof(this.Label), StringComparison.Ordinal) == true)
        {
            this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.Label)));
        }
        else if (args.PropertyName?.Equals(nameof(this.IsExpanded), StringComparison.Ordinal) == true)
        {
            this.IsExpanded = this.payload.IsExpanded;
        }
    }

    /// <inheritdoc />
    protected override void OnPropertyChanged(PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);

        if (e.PropertyName == nameof(this.IsExpanded))
        {
            this.payload.IsExpanded = this.IsExpanded;
        }
    }
}
