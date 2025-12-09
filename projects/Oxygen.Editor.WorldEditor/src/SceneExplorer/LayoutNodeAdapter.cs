// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
using System.ComponentModel;
using System.Collections.ObjectModel;
using DroidNet.Controls;
using Oxygen.Editor.Core;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
/// Layout-first adapter that wraps a <see cref="SceneNode" /> payload. This adapter owns
/// its layout children (which can be a mix of folders and other adapters). The layout
/// is the authoritative source for the explorer view; scene graph propagation must be
/// performed explicitly when required.
/// </summary>
public sealed class LayoutNodeAdapter : TreeItemAdapter, ITreeItem<SceneNode>
{
    private SceneNode payload;

    // Layout-owned children (folders and node adapters). This is the authoritative
    // children list for the explorer UI.
    private readonly ObservableCollection<ITreeItem> layoutChildren = new();

    public LayoutNodeAdapter(SceneNode payload)
    {
        this.payload = payload ?? throw new ArgumentNullException(nameof(payload));
        this.IsExpanded = this.payload.IsExpanded;
        this.payload.PropertyChanged += this.OnPayloadPropertyChanged;

        // Keep icon/label reactive when layout children change
        this.layoutChildren.CollectionChanged += (_, _) => this.OnPropertyChanged(nameof(this.IconGlyph));
    }

    /// <summary>
    /// Gets the wrapped scene node payload.
    /// </summary>
    public SceneNode AttachedObject => this.payload;

    public override string Label
    {
        get => this.payload.Name;
        set
        {
            if (string.Equals(value, this.payload.Name, StringComparison.Ordinal))
            {
                return;
            }

            this.payload.Name = value;
            this.OnPropertyChanged();
        }
    }

    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <summary>
    /// Glyph used for thumbnails or icons. Mirrors FolderAdapter's behavior:
    /// open-folder glyph when expanded and has children, otherwise closed-folder.
    /// For nodes we show a node glyph when no layout children exist.
    /// </summary>
    public string IconGlyph
    {
        get
        {
            if (this.layoutChildren.Count > 0)
            {
                return this.IsExpanded ? "\uE838" : "\uE8B7";
            }

            // Default node glyph
            return "\uE7C1";
        }
    }

    /// <summary>
    /// Add a layout child to this adapter.
    /// </summary>
    public void AddLayoutChild(ITreeItem child)
    {
        if (child is TreeItemAdapter adapter)
        {
            this.layoutChildren.Add(adapter);
            if (this.IsExpanded)
            {
                var realizedChildren = this.Children.ConfigureAwait(false).GetAwaiter().GetResult();
                if (!realizedChildren.Contains(adapter))
                {
                    this.AddChildInternal(adapter);
                }
            }
        }
        else
        {
            this.layoutChildren.Add(child);
        }
    }

    public bool RemoveLayoutChild(ITreeItem child)
    {
        var removed = this.layoutChildren.Remove(child);
        if (removed && child is TreeItemAdapter adapter)
        {
            _ = this.Children.ConfigureAwait(false);
            _ = this.RemoveChildAsync(adapter).GetAwaiter().GetResult();
        }

        return removed;
    }

    public void ReplaceLayoutChildren(IEnumerable<ITreeItem> children)
    {
        this.layoutChildren.Clear();
        foreach (var c in children)
        {
            this.layoutChildren.Add(c);
        }
    }

    public IReadOnlyList<ITreeItem> LayoutChildren => this.layoutChildren.ToList();

    protected override int DoGetChildrenCount() => this.layoutChildren.Count > 0 ? this.layoutChildren.Count : this.payload.Children.Count;

    protected override async Task LoadChildren()
    {
        this.ClearChildren();
        if (this.layoutChildren.Count > 0)
        {
            foreach (var child in this.layoutChildren)
            {
                if (child is TreeItemAdapter adapter)
                {
                    this.AddChildInternal(adapter);
                }
            }
        }
        else
        {
            // Fall back to payload's scene-graph children when no layout is present.
            foreach (var node in this.payload.Children)
            {
                var childAdapter = new LayoutNodeAdapter(node);
                this.AddChildInternal(childAdapter);
            }
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    private void OnPayloadPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        // Payload notifies property changes by its own property names (e.g. "Name", "IsExpanded").
        if (string.Equals(args.PropertyName, nameof(SceneNode.Name), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.Label)));
        }
        else if (string.Equals(args.PropertyName, nameof(SceneNode.IsExpanded), StringComparison.Ordinal))
        {
            this.IsExpanded = this.payload.IsExpanded;
        }
    }

    public void AttachPayload(SceneNode newPayload)
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

    protected override void OnPropertyChanged(PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);

        if (e.PropertyName == nameof(this.IsExpanded))
        {
            this.payload.IsExpanded = this.IsExpanded;
        }
    }
}
