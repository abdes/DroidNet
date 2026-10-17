// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Scripting;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Scripting;

/// <summary>
/// The slot changes the inspector offers: values equal to the default are not stored, values the script no longer
/// declares are found as stale, and slots reorder within bounds.
/// </summary>
[TestClass]
public sealed class ScriptSlotEditsTests
{
    private static readonly ScriptParameter Speed = new("speed", ScriptParameterKind.Float, 1f, 0, Min: 0, Max: null, Step: null, "m/s", string.Empty, [], IsColor: false);
    private static readonly ScriptParameter Target = new("target", ScriptParameterKind.Node, DefaultValue: null, 0, Min: null, Max: null, Step: null, string.Empty, string.Empty, [], IsColor: false);

    [TestMethod]
    public void SetValueStoresOnlyValuesThatDifferFromTheDefault()
    {
        ImmutableArray<ScriptSlot> slots = [new ScriptSlot()];
        var slot = slots[0].Id;

        var changed = ScriptSlotEdits.SetValue(slots, slot, Speed, ScriptValue.FromFloat(3f));
        var back = ScriptSlotEdits.SetValue(changed, slot, Speed, ScriptValue.FromFloat(1f));

        _ = changed[0].Overrides["speed"].FloatValue.Should().Be(3f);
        _ = back[0].Overrides.Should().BeEmpty();
    }

    [TestMethod]
    public void ResetAndNullRemoveAReference()
    {
        var node = NodeId.New();
        ImmutableArray<ScriptSlot> slots = [new ScriptSlot().WithOverride("target", ScriptValue.FromNode(node))];

        _ = ScriptSlotEdits.SetValue(slots, slots[0].Id, Target, value: null)[0].Overrides.Should().BeEmpty();
        _ = ScriptSlotEdits.Reset(slots, slots[0].Id, "target")[0].Overrides.Should().BeEmpty();
    }

    [TestMethod]
    public void FindStaleNamesUndeclaredAndRetypedValues()
    {
        var slot = new ScriptSlot()
            .WithOverride("speed", ScriptValue.FromInt(3))
            .WithOverride("gone", ScriptValue.FromBool(value: true))
            .WithOverride("target", ScriptValue.FromNode(NodeId.New()));

        var stale = ScriptSlotEdits.FindStale(slot, [Speed, Target]);
        var cleaned = ScriptSlotEdits.RemoveStale([slot], slot.Id, [Speed, Target]);

        _ = stale.Should().Equal("gone", "speed");
        _ = cleaned[0].Overrides.Keys.Should().Equal("target");
    }

    [TestMethod]
    public void MoveKeepsSlotsWithinTheirEnds()
    {
        ImmutableArray<ScriptSlot> slots = [new ScriptSlot(), new ScriptSlot(), new ScriptSlot()];

        _ = ScriptSlotEdits.Move(slots, slots[2].Id, -1).Should().Equal(slots[0], slots[2], slots[1]);
        _ = ScriptSlotEdits.Move(slots, slots[0].Id, -1).Should().Equal(slots);
        _ = ScriptSlotEdits.Remove(slots, slots[1].Id).Should().Equal(slots[0], slots[2]);
    }
}
