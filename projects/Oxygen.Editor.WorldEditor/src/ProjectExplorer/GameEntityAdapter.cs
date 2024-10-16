// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

using DroidNet.Controls;
using Oxygen.Editor.Projects;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for the <see cref="GameEntity" /> model class.
/// </summary>
/// <param name="gameEntity">The <see cref="GameEntity" /> object to wrap as a <see cref="ITreeItem" />.</param>
public partial class GameEntityAdapter(GameEntity gameEntity) : TreeItemAdapter, ITreeItem<GameEntity>
{
    public override bool IsRoot => false;

    public override string Label
        => this.AttachedObject.Name;

    public GameEntity AttachedObject => gameEntity;

    protected override int GetChildrenCount() => 0;

    protected override async Task LoadChildren() => await Task.CompletedTask.ConfigureAwait(false);
}
