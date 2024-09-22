// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.Demo.Model;

public class EntityAdapter(Entity item) : TreeItemAdapter, ITreeItem<Entity>
{
    public override bool IsRoot => false;

    public override string Label
        => this.AttachedObject.Name;

    public Entity AttachedObject => item;

    protected override async Task LoadChildren() => await Task.CompletedTask.ConfigureAwait(false);
}
