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
        var before = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First);

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotExecutionOrder(ScriptSlotCases.Node, ScriptSlotCases.First, Order: -3)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First).Should().Be(before with { ExecutionOrder = -3 });
        _ = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.Second).ExecutionOrder.Should().Be(0);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScriptSlotsChanged(ScriptSlotCases.Node));
    }

    [TestMethod]
    public async Task Undo_RestoresTheOrder()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = ScriptSlotCases.SlotsOf(fixture);
        _ = await fixture.Authoring.ExecuteAsync(new SetSlotExecutionOrder(ScriptSlotCases.Node, ScriptSlotCases.First, Order: 7)).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = ScriptSlotCases.SlotsOf(fixture).Should().Equal(before);
    }

    [TestMethod]
    public async Task SameOrder_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotExecutionOrder(ScriptSlotCases.Node, ScriptSlotCases.First, Order: 0)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SlotNotOnTheNode_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotExecutionOrder(ScriptSlotCases.Node, ScriptSlotId.New(), Order: 1)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing, ScriptSlotCases.Node);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodeWithoutScripting_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotExecutionOrder(SampleScene.B, ScriptSlotCases.First, Order: 1)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }
}
