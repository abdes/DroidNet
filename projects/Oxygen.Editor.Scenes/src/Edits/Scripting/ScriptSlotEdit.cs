// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Settings;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Scripting;

/// <summary>
/// What the edits of one script slot share: finding the node's Scripting component and the slot, and applying a change
/// to the slot list so that an unchanged list is no undo entry and the inverse restores the whole list.
/// </summary>
internal static class ScriptSlotEdit
{
    /// <summary>Finds a node's Scripting component.</summary>
    /// <param name="scope">The edit scope.</param>
    /// <param name="node">The node.</param>
    /// <returns>The component, or <see langword="null"/> when the node or the component is missing.</returns>
    internal static ScriptingComponent? ScriptingOf(SceneEditScope scope, NodeId node)
        => scope.FindNode(node)?.Components.OfType<ScriptingComponent>().FirstOrDefault();

    /// <summary>Checks that the node has a Scripting component, and optionally that it holds the slot.</summary>
    /// <param name="scope">The edit scope.</param>
    /// <param name="node">The node.</param>
    /// <param name="slot">The slot the edit addresses, or <see langword="null"/> when the edit adds it.</param>
    /// <returns>The invalid validation, or <see langword="null"/> when the node and the slot are there.</returns>
    internal static SceneEditValidation? Check(SceneEditScope scope, NodeId node, ScriptSlotId? slot)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (ScriptingOf(scope, node) is not { } scripting)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node no longer has a Scripting component.", node));
        }

        return slot is { } id && IndexOf(scripting.Slots, id) < 0
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.EntryMissing, "The node no longer has that script slot.", node))
            : null;
    }

    /// <summary>Finds a slot in a list.</summary>
    /// <param name="slots">The slots.</param>
    /// <param name="slot">The slot identity.</param>
    /// <returns>The index, or -1 when the list has no such slot.</returns>
    internal static int IndexOf(ImmutableArray<ScriptSlot> slots, ScriptSlotId slot)
    {
        for (var index = 0; index < slots.Length; index++)
        {
            if (slots[index].Id == slot.Value)
            {
                return index;
            }
        }

        return -1;
    }

    /// <summary>Applies a change to the node's slot list.</summary>
    /// <param name="edit">The edit, returned as its own inverse when the list does not change.</param>
    /// <param name="scope">The edit scope.</param>
    /// <param name="node">The node.</param>
    /// <param name="change">Computes the new list from the current one.</param>
    /// <param name="referenced">The asset references the change writes, warned about when an asset was deleted.</param>
    /// <returns>The application; its inverse restores the whole list.</returns>
    internal static SceneEditApplication Apply(
        ISceneEdit edit,
        SceneEditScope scope,
        NodeId node,
        Func<ImmutableArray<ScriptSlot>, ImmutableArray<ScriptSlot>> change,
        IEnumerable<AssetPath?>? referenced = null)
    {
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentNullException.ThrowIfNull(scope);
        ArgumentNullException.ThrowIfNull(change);
        var scripting = ScriptingOf(scope, node)!;
        var before = scripting.Slots;
        var after = change(before);
        if (before.SequenceEqual(after))
        {
            return new SceneEditApplication.Applied(edit, []);
        }

        scope.WarnIfDeleted(referenced ?? [], node);
        scripting.Slots = after;
        return new SceneEditApplication.Applied(new SetScriptSlots(node, before), [new SceneChange.ScriptSlotsChanged(node)]);
    }

    /// <summary>Applies a change to one slot of the node.</summary>
    /// <param name="edit">The edit, returned as its own inverse when the list does not change.</param>
    /// <param name="scope">The edit scope.</param>
    /// <param name="node">The node.</param>
    /// <param name="slot">The slot.</param>
    /// <param name="update">Computes the new slot from the current one.</param>
    /// <param name="referenced">The asset references the change writes, warned about when an asset was deleted.</param>
    /// <returns>The application; its inverse restores the whole list.</returns>
    internal static SceneEditApplication Apply(
        ISceneEdit edit,
        SceneEditScope scope,
        NodeId node,
        ScriptSlotId slot,
        Func<ScriptSlot, ScriptSlot> update,
        IEnumerable<AssetPath?>? referenced = null)
    {
        ArgumentNullException.ThrowIfNull(update);
        return Apply(
            edit,
            scope,
            node,
            slots =>
            {
                var index = IndexOf(slots, slot);
                return slots.SetItem(index, update(slots[index]));
            },
            referenced);
    }
}
