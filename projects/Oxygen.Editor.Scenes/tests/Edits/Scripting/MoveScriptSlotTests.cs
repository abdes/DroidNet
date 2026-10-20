// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Scripting;
using Oxygen.Editor.Scenes.Tests.TestSupport;

namespace Oxygen.Editor.Scenes.Tests.Edits.Scripting;

/// <summary>
/// What is specific to <see cref="MoveScriptSlot"/>: a slot moves one place up or down and stays within the ends of the
/// list. Targets, locks, undo and the change stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class MoveScriptSlotTests
{
    [TestMethod]
    public async Task Down_SwapsWithTheNextSlot()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new MoveScriptSlot(SampleScene.Script, SampleScene.MoveSlotId, Offset: 1)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.SlotsOf(SampleScene.Script).Select(static slot => slot.Id).Should().Equal(SampleScene.SecondSlotId.Value, SampleScene.MoveSlotId.Value);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScriptSlotsChanged(SampleScene.Script));
    }

    [TestMethod]
    public async Task Up_SwapsWithThePreviousSlot()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new MoveScriptSlot(SampleScene.Script, SampleScene.SecondSlotId, Offset: -1)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.SlotsOf(SampleScene.Script).Select(static slot => slot.Id).Should().Equal(SampleScene.SecondSlotId.Value, SampleScene.MoveSlotId.Value);
    }

    [TestMethod]
    public async Task Undo_RestoresTheOrder()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.SlotsOf(SampleScene.Script);
        _ = await fixture.Authoring.ExecuteAsync(new MoveScriptSlot(SampleScene.Script, SampleScene.MoveSlotId, Offset: 1)).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = fixture.SlotsOf(SampleScene.Script).Should().Equal(before);
    }

    [TestMethod]
    public async Task Up_AtTheTop_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new MoveScriptSlot(SampleScene.Script, SampleScene.MoveSlotId, Offset: -1)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Down_AtTheBottom_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new MoveScriptSlot(SampleScene.Script, SampleScene.SecondSlotId, Offset: 1)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SlotNotOnTheNode_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new MoveScriptSlot(SampleScene.Script, ScriptSlotId.New(), Offset: 1)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing, SampleScene.Script);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodeWithoutScripting_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new MoveScriptSlot(SampleScene.B, SampleScene.MoveSlotId, Offset: 1)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }
}
