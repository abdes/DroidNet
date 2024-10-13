// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.Demo.Model;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for the <see cref="Entity" /> model class.
/// </summary>
/// <param name="entity">The <see cref="Entity" /> object to wrap as a <see cref="ITreeItem" />.</param>
public partial class EntityAdapter(Entity entity) : TreeItemAdapter, ITreeItem<Entity>
{
    public override bool IsRoot => false;

    public override string Label
        => this.AttachedObject.Name;

    public Entity AttachedObject => entity;

    protected override int GetChildrenCount() => 0;

    protected override async Task LoadChildren() => await Task.CompletedTask.ConfigureAwait(false);
}
