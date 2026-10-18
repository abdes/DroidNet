// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Numerics;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Scripting;

/// <summary>
/// The slot changes the inspector offers, as pure functions from one slot list to the next. The command service
/// applies the result as one undoable edit.
/// </summary>
internal static class ScriptSlotEdits
{
    /// <summary>Adds an empty slot at the end.</summary>
    /// <param name="slots">The slots.</param>
    /// <returns>The slots with the new slot.</returns>
    public static ImmutableArray<ScriptSlot> Add(ImmutableArray<ScriptSlot> slots) => slots.Add(new ScriptSlot());

    /// <summary>Removes a slot.</summary>
    /// <param name="slots">The slots.</param>
    /// <param name="slotId">The slot to remove.</param>
    /// <returns>The slots without it.</returns>
    public static ImmutableArray<ScriptSlot> Remove(ImmutableArray<ScriptSlot> slots, Guid slotId)
        => slots.RemoveAll(slot => slot.Id == slotId);

    /// <summary>Moves a slot up or down.</summary>
    /// <param name="slots">The slots.</param>
    /// <param name="slotId">The slot to move.</param>
    /// <param name="offset">-1 to move up, 1 to move down.</param>
    /// <returns>The reordered slots; unchanged when the slot is already at that end.</returns>
    public static ImmutableArray<ScriptSlot> Move(ImmutableArray<ScriptSlot> slots, Guid slotId, int offset)
    {
        var index = IndexOf(slots, slotId);
        var target = index + offset;
        if (index < 0 || target < 0 || target >= slots.Length)
        {
            return slots;
        }

        var slot = slots[index];
        return slots.RemoveAt(index).Insert(target, slot);
    }

    /// <summary>Picks a slot's script. Parameter values are kept; values the new script does not declare become
    /// stale.</summary>
    /// <param name="slots">The slots.</param>
    /// <param name="slotId">The slot.</param>
    /// <param name="script">The script identity, or null to clear it.</param>
    /// <returns>The updated slots.</returns>
    public static ImmutableArray<ScriptSlot> SetScript(ImmutableArray<ScriptSlot> slots, Guid slotId, Uri? script)
        => Update(slots, slotId, slot => slot with { Script = script });

    /// <summary>Sets a slot's execution order.</summary>
    /// <param name="slots">The slots.</param>
    /// <param name="slotId">The slot.</param>
    /// <param name="order">The execution order; lower runs earlier.</param>
    /// <returns>The updated slots.</returns>
    public static ImmutableArray<ScriptSlot> SetExecutionOrder(ImmutableArray<ScriptSlot> slots, Guid slotId, int order)
        => Update(slots, slotId, slot => slot with { ExecutionOrder = order });

    /// <summary>Sets a parameter value. A value equal to the declared default removes the override, so later changes
    /// to the default apply.</summary>
    /// <param name="slots">The slots.</param>
    /// <param name="slotId">The slot.</param>
    /// <param name="parameter">The declared parameter.</param>
    /// <param name="value">The value, or null to take the default.</param>
    /// <returns>The updated slots.</returns>
    public static ImmutableArray<ScriptSlot> SetValue(ImmutableArray<ScriptSlot> slots, Guid slotId, ScriptParameter parameter, ScriptValue? value)
    {
        ArgumentNullException.ThrowIfNull(parameter);
        return Update(slots, slotId, slot => value is null || value.Equals(DefaultOf(parameter))
            ? slot.WithoutOverrides([parameter.Name])
            : slot.WithOverride(parameter.Name, value));
    }

    /// <summary>Resets a parameter to its declared default.</summary>
    /// <param name="slots">The slots.</param>
    /// <param name="slotId">The slot.</param>
    /// <param name="name">The parameter name.</param>
    /// <returns>The updated slots.</returns>
    public static ImmutableArray<ScriptSlot> Reset(ImmutableArray<ScriptSlot> slots, Guid slotId, string name)
        => Update(slots, slotId, slot => slot.WithoutOverrides([name]));

    /// <summary>Removes the values the slot's script no longer declares, or declares with another type.</summary>
    /// <param name="slots">The slots.</param>
    /// <param name="slotId">The slot.</param>
    /// <param name="declared">The script's declared parameters.</param>
    /// <returns>The updated slots.</returns>
    public static ImmutableArray<ScriptSlot> RemoveStale(ImmutableArray<ScriptSlot> slots, Guid slotId, IReadOnlyList<ScriptParameter> declared)
        => Update(slots, slotId, slot => slot.WithoutOverrides(FindStale(slot, declared)));

    /// <summary>Finds the values a slot holds that its script does not declare with that type.</summary>
    /// <param name="slot">The slot.</param>
    /// <param name="declared">The script's declared parameters.</param>
    /// <returns>The stale parameter names, in name order.</returns>
    public static IReadOnlyList<string> FindStale(ScriptSlot slot, IReadOnlyList<ScriptParameter> declared)
    {
        ArgumentNullException.ThrowIfNull(slot);
        ArgumentNullException.ThrowIfNull(declared);
        return [.. slot.Overrides
            .Where(entry => declared.FirstOrDefault(parameter => string.Equals(parameter.Name, entry.Key, StringComparison.Ordinal)) is not { } parameter
                || ToValueKind(parameter.Kind) != entry.Value.Kind)
            .Select(static entry => entry.Key)];
    }

    /// <summary>Maps a declared parameter type to the value type a slot stores.</summary>
    /// <param name="kind">The declared type.</param>
    /// <returns>The value type, or null when the declaration has no type.</returns>
    public static ScriptValueKind? ToValueKind(ScriptParameterKind kind)
        => kind switch
        {
            ScriptParameterKind.Bool => ScriptValueKind.BoolValue,
            ScriptParameterKind.Int32 => ScriptValueKind.Int32Value,
            ScriptParameterKind.Float => ScriptValueKind.FloatValue,
            ScriptParameterKind.String => ScriptValueKind.StringValue,
            ScriptParameterKind.Vec2 => ScriptValueKind.Vec2Value,
            ScriptParameterKind.Vec3 => ScriptValueKind.Vec3Value,
            ScriptParameterKind.Vec4 => ScriptValueKind.Vec4Value,
            ScriptParameterKind.Asset => ScriptValueKind.AssetValue,
            ScriptParameterKind.Node => ScriptValueKind.NodeValue,
            _ => null,
        };

    /// <summary>Gets a parameter's declared default as a slot value.</summary>
    /// <param name="parameter">The declared parameter.</param>
    /// <returns>The default, or null for asset and node parameters, which default to none.</returns>
    public static ScriptValue? DefaultOf(ScriptParameter parameter)
    {
        ArgumentNullException.ThrowIfNull(parameter);
        return parameter.DefaultValue switch
        {
            bool value => ScriptValue.FromBool(value),
            int value => ScriptValue.FromInt(value),
            float value => ScriptValue.FromFloat(value),
            string value => ScriptValue.FromString(value),
            Vector2 value => ScriptValue.FromVector(value),
            Vector3 value => ScriptValue.FromVector(value),
            Vector4 value => ScriptValue.FromVector(value),
            _ => null,
        };
    }

    private static ImmutableArray<ScriptSlot> Update(ImmutableArray<ScriptSlot> slots, Guid slotId, Func<ScriptSlot, ScriptSlot> update)
    {
        var index = IndexOf(slots, slotId);
        return index < 0 ? slots : slots.SetItem(index, update(slots[index]));
    }

    private static int IndexOf(ImmutableArray<ScriptSlot> slots, Guid slotId)
    {
        for (var index = 0; index < slots.Length; index++)
        {
            if (slots[index].Id == slotId)
            {
                return index;
            }
        }

        return -1;
    }
}
