// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.Tree.Model;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for the <see cref="Entity" /> model class.
/// </summary>
/// <param name="entity">The <see cref="Entity" /> object to wrap as a <see cref="ITreeItem" />.</param>
internal sealed partial class EntityAdapter(Entity entity) : TreeItemAdapter(isRoot: false, isHidden: false), ITreeItem<Entity>, ICanBeCloned
{
    private string label = entity.Name;

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
    public Entity AttachedObject => entity;

    /// <summary>Gets or sets a value indicating whether the demo entity is visible, independently of tree selection.</summary>
    public bool IsVisible
    {
        get => entity.IsVisible;
        set
        {
            if (entity.IsVisible == value)
            {
                return;
            }

            entity.IsVisible = value;
            this.OnPropertyChanged();
            this.OnPropertyChanged(nameof(this.VisibilityGlyph));
            this.OnPropertyChanged(nameof(this.VisibilityAction));
        }
    }

    /// <summary>Gets or sets a value indicating whether demo content is resident, independently of lazy child enumeration.</summary>
    public bool IsLoaded
    {
        get => entity.IsLoaded;
        set
        {
            if (entity.IsLoaded == value)
            {
                return;
            }

            entity.IsLoaded = value;
            this.OnPropertyChanged();
            this.OnPropertyChanged(nameof(this.LoadedGlyph));
            this.OnPropertyChanged(nameof(this.LoadedStatus));
        }
    }

    /// <summary>Gets the visible or hidden state glyph.</summary>
    public string VisibilityGlyph => this.IsVisible ? "\uE890" : "\uED1A";

    /// <summary>Gets the accessible action offered by the visibility button.</summary>
    public string VisibilityAction => this.IsVisible ? "Hide entity" : "Show entity";

    /// <summary>Gets the document glyph for loaded content, or no glyph for unloaded content.</summary>
    public string LoadedGlyph => this.IsLoaded ? "\uE8A5" : string.Empty;

    /// <summary>Gets the noninteractive content-residency description.</summary>
    public string LoadedStatus => this.IsLoaded ? "Loaded" : "Unloaded";

    /// <inheritdoc/>
    public override bool ValidateItemName(string name) => name.Trim().Length != 0;

    /// <inheritdoc/>
    public ITreeItem CloneSelf()
    {
        var cloneModel = this.AttachedObject.CloneWithoutChildren();
        var clone = new EntityAdapter(cloneModel);
        this.CopyBasePropertiesTo(clone);
        return clone;
    }

    /// <inheritdoc/>
    protected override int DoGetChildrenCount() => this.AttachedObject.Entities.Count;

    /// <inheritdoc/>
    protected override async Task LoadChildren()
    {
        foreach (var child in this.AttachedObject.Entities)
        {
            this.AddChildInternal(
                new EntityAdapter(child)
                {
                    IsExpanded = false,
                });
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }
}
