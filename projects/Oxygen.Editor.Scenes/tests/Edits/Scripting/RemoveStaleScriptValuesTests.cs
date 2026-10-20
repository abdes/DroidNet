// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Scripting;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.Scenes.Tests.Edits.Scripting;

/// <summary>
/// What is specific to <see cref="RemoveStaleScriptValues"/>: values the script does not declare, or declares with another
/// type, are removed and the others are kept. Targets, locks, undo and the change stream of every edit are
/// <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class RemoveStaleScriptValuesTests
{
    [TestMethod]
    public async Task UndeclaredAndRetypedValues_AreRemoved()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new SetScriptValue(SampleScene.Script, SampleScene.MoveSlotId, SampleScene.SpeedParameter, ScriptValue.FromFloat(3f))).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new SetScriptValue(SampleScene.Script, SampleScene.MoveSlotId, new ScriptParameter { Name = "gone", Kind = ScriptParameterKind.BoolParameter }, ScriptValue.FromBool(value: true))).ConfigureAwait(false);
        fixture.Changes.Clear();

        // The script now declares "speed" as an integer and no longer declares "gone"; "target" is still a node.
        var declared = new[] { SampleScene.SpeedParameter with { Kind = ScriptParameterKind.Int32Parameter, Default = ScriptValue.FromInt(1) }, SampleScene.TargetParameter };
        var result = await fixture.Authoring.ExecuteAsync(new RemoveStaleScriptValues(SampleScene.Script, SampleScene.MoveSlotId, declared)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.SlotOf(SampleScene.Script, SampleScene.MoveSlotId).Overrides.Keys.Should().Equal("target");
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScriptSlotsChanged(SampleScene.Script));
    }

    [TestMethod]
    public async Task Undo_RestoresTheValues()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.SlotsOf(SampleScene.Script);
        _ = await fixture.Authoring.ExecuteAsync(new RemoveStaleScriptValues(SampleScene.Script, SampleScene.MoveSlotId, [])).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = fixture.SlotsOf(SampleScene.Script).Should().Equal(before);
    }

    [TestMethod]
    public async Task NothingStale_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveStaleScriptValues(SampleScene.Script, SampleScene.MoveSlotId, [SampleScene.TargetParameter])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SlotNotOnTheNode_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveStaleScriptValues(SampleScene.Script, ScriptSlotId.New(), [])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing, SampleScene.Script);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodeWithoutScripting_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveStaleScriptValues(SampleScene.B, SampleScene.MoveSlotId, [])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }
}
