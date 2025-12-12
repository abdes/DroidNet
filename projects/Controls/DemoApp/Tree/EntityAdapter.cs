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

    /// <inheritdoc/>
    public override bool ValidateItemName(string name) => name.Trim().Length != 0;

    /// <inheritdoc/>
    public ITreeItem CloneSelf()
    {
        var cloneModel = new Entity(this.AttachedObject.Name);
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
