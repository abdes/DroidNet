// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits.Scripting;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Scripting;

/// <summary>What the script slot edit tests share: the sample node's two slots and the parameters its script declares.</summary>
internal static class ScriptSlotCases
{
    /// <summary>Gets the sample node, whose Scripting component holds <see cref="First"/> and <see cref="Second"/>.</summary>
    public static NodeId Node => SampleScene.Script;

    /// <summary>Gets the first slot, which holds a "target" value naming node B.</summary>
    public static ScriptSlotId First { get; } = ScriptSlotId.FromValue(SampleScene.MoveSlot);

    /// <summary>Gets the second slot, which holds no values.</summary>
    public static ScriptSlotId Second { get; } = ScriptSlotId.FromValue(SampleScene.SecondSlot);

    /// <summary>Gets a float parameter whose default is 1.</summary>
    public static ScriptParameter Speed { get; } = new() { Name = "speed", Kind = ScriptParameterKind.FloatParameter, Default = ScriptValue.FromFloat(1f) };

    /// <summary>Gets a node parameter, which defaults to none.</summary>
    public static ScriptParameter Target { get; } = new() { Name = "target", Kind = ScriptParameterKind.NodeParameter };

    /// <summary>Reads the slots of the sample node.</summary>
    /// <param name="fixture">The fixture.</param>
    /// <returns>The slots, in order.</returns>
    public static IReadOnlyList<ScriptSlot> SlotsOf(AuthoringFixture fixture)
    {
        ArgumentNullException.ThrowIfNull(fixture);
        return [.. fixture.Node(Node).Components.OfType<ScriptingComponent>().Single().Slots];
    }

    /// <summary>Reads the identities of the slots of the sample node.</summary>
    /// <param name="fixture">The fixture.</param>
    /// <returns>The identities, in order.</returns>
    public static IReadOnlyList<Guid> IdsOf(AuthoringFixture fixture) => [.. SlotsOf(fixture).Select(static slot => slot.Id)];

    /// <summary>Finds a slot of the sample node.</summary>
    /// <param name="fixture">The fixture.</param>
    /// <param name="slot">The slot.</param>
    /// <returns>The slot.</returns>
    public static ScriptSlot SlotOf(AuthoringFixture fixture, ScriptSlotId slot) => SlotsOf(fixture).Single(item => item.Id == slot.Value);
}
