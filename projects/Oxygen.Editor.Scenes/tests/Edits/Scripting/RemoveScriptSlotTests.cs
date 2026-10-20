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
/// What is specific to <see cref="RemoveScriptSlot"/>: the slot goes and the others keep their order. Targets, locks,
/// undo and the change stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class RemoveScriptSlotTests
{
    [TestMethod]
    public async Task Slot_IsRemovedAndTheOthersKeepTheirOrder()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new RemoveScriptSlot(ScriptSlotCases.Node, ScriptSlotCases.First)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = ScriptSlotCases.IdsOf(fixture).Should().Equal(ScriptSlotCases.Second.Value);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScriptSlotsChanged(ScriptSlotCases.Node));
    }

    [TestMethod]
    public async Task Undo_RestoresTheSlotWithItsValuesAtItsPlace()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = ScriptSlotCases.SlotsOf(fixture);
        _ = await fixture.Authoring.ExecuteAsync(new RemoveScriptSlot(ScriptSlotCases.Node, ScriptSlotCases.First)).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = ScriptSlotCases.SlotsOf(fixture).Should().Equal(before);
    }

    [TestMethod]
    public async Task SlotNotOnTheNode_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveScriptSlot(ScriptSlotCases.Node, ScriptSlotId.New())).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing, ScriptSlotCases.Node);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodeWithoutScripting_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveScriptSlot(SampleScene.B, ScriptSlotCases.First)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }
}
