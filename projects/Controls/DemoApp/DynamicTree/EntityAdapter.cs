// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.Demo.Model;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for the <see cref="Entity" /> model class.
/// </summary>
/// <param name="entity">The <see cref="Entity" /> object to wrap as a <see cref="ITreeItem" />.</param>
public partial class EntityAdapter(Entity entity) : TreeItemAdapter(isRoot: false, isHidden: false), ITreeItem<Entity>
{
    private string label = entity.Name;

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

    public Entity AttachedObject => entity;

    public override bool ValidateItemName(string name) => name.Trim().Length != 0;

    protected override int DoGetChildrenCount() => 0;

    protected override async Task LoadChildren() => await Task.CompletedTask.ConfigureAwait(false);
}
