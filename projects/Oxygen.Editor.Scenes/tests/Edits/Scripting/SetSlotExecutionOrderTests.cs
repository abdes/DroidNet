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
/// What is specific to <see cref="SetSlotExecutionOrder"/>: the order of one slot changes and nothing else does.
/// Targets, locks, undo and the change stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetSlotExecutionOrderTests
{
    [TestMethod]
    public async Task Order_IsSetOnTheSlotAlone()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.SlotOf(SampleScene.Script, SampleScene.MoveSlotId);

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotExecutionOrder(SampleScene.Script, SampleScene.MoveSlotId, Order: -3)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.SlotOf(SampleScene.Script, SampleScene.MoveSlotId).Should().Be(before with { ExecutionOrder = -3 });
        _ = fixture.SlotOf(SampleScene.Script, SampleScene.SecondSlotId).ExecutionOrder.Should().Be(0);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScriptSlotsChanged(SampleScene.Script));
    }

    [TestMethod]
    public async Task Undo_RestoresTheOrder()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.SlotsOf(SampleScene.Script);
        _ = await fixture.Authoring.ExecuteAsync(new SetSlotExecutionOrder(SampleScene.Script, SampleScene.MoveSlotId, Order: 7)).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = fixture.SlotsOf(SampleScene.Script).Should().Equal(before);
    }

    [TestMethod]
    public async Task SameOrder_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotExecutionOrder(SampleScene.Script, SampleScene.MoveSlotId, Order: 0)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SlotNotOnTheNode_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotExecutionOrder(SampleScene.Script, ScriptSlotId.New(), Order: 1)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing, SampleScene.Script);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodeWithoutScripting_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotExecutionOrder(SampleScene.B, SampleScene.MoveSlotId, Order: 1)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }
}
