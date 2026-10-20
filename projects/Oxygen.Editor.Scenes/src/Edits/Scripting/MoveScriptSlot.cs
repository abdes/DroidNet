// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Scripting;

/// <summary>Moves one script slot up or down in a node's Scripting component.</summary>
/// <remarks>A slot already at that end stays where it is, which is no change and no undo entry.</remarks>
/// <param name="Node">The node whose Scripting component holds the slots.</param>
/// <param name="Slot">The slot to move.</param>
/// <param name="Offset">-1 to move up, 1 to move down.</param>
public sealed record MoveScriptSlot(NodeId Node, ScriptSlotId Slot, int Offset) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Move Script Slot";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Node];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        return ScriptSlotEdit.Check(scope, this.Node, this.Slot) ?? SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
        => ScriptSlotEdit.Apply(
            this,
            scope,
            this.Node,
            slots =>
            {
                var index = ScriptSlotEdit.IndexOf(slots, this.Slot);
                var target = index + this.Offset;
                return target < 0 || target >= slots.Length
                    ? slots
                    : slots.RemoveAt(index).Insert(target, slots[index]);
            });
}
