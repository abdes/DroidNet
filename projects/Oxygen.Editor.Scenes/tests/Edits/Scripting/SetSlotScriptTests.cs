// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Scripting;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Scripting;

/// <summary>
/// What is specific to <see cref="SetSlotScript"/>: the slot's script is picked or cleared, its values are kept, and the
/// reference follows the asset. Targets, locks, undo and the change stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetSlotScriptTests
{
    private static AssetPath Jump { get; } = AssetPath.Parse("/Content/Jump.oscript");

    [TestMethod]
    public async Task Script_IsPickedAndTheValuesAreKept()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.SlotOf(SampleScene.Script, SampleScene.MoveSlotId);

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(SampleScene.Script, SampleScene.MoveSlotId, Jump)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var slot = fixture.SlotOf(SampleScene.Script, SampleScene.MoveSlotId);
        _ = slot.Script.Should().Be(Jump);
        _ = slot.Overrides.Should().ContainKey("target", "a value the new script does not declare becomes stale, not lost");
        _ = slot.Should().Be(before with { Script = Jump });
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScriptSlotsChanged(SampleScene.Script));
    }

    [TestMethod]
    public async Task Null_ClearsTheScript()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(SampleScene.Script, SampleScene.MoveSlotId, Script: null)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.SlotOf(SampleScene.Script, SampleScene.MoveSlotId).Script.Should().BeNull();
    }

    [TestMethod]
    public async Task Undo_RestoresTheScript()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.SlotsOf(SampleScene.Script);
        _ = await fixture.Authoring.ExecuteAsync(new SetSlotScript(SampleScene.Script, SampleScene.MoveSlotId, Jump)).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = fixture.SlotsOf(SampleScene.Script).Should().Equal(before);
    }

    [TestMethod]
    public async Task SameScript_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(SampleScene.Script, SampleScene.MoveSlotId, SampleScene.MoveScript)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task ScriptThatMoved_IsFollowedToItsNewIdentity()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var moved = EditSamples.MovedTo(Jump);
        fixture.Redirects.Move(Jump, moved);

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(SampleScene.Script, SampleScene.MoveSlotId, Jump)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.SlotOf(SampleScene.Script, SampleScene.MoveSlotId).Script.Should().Be(moved);
    }

    [TestMethod]
    public async Task ScriptThatWasDeleted_IsKeptWithAWarning()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Redirects.Delete(Jump);

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(SampleScene.Script, SampleScene.MoveSlotId, Jump)).ConfigureAwait(false);

        SceneAssert.Warned(result, SceneEditCodes.AssetReferenceDeleted, SampleScene.Script);
        _ = fixture.SlotOf(SampleScene.Script, SampleScene.MoveSlotId).Script.Should().Be(Jump);
    }

    [TestMethod]
    public async Task AssetThatIsNotAScript_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(SampleScene.Script, SampleScene.MoveSlotId, SampleScene.RedMaterial)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AssetReferenceInvalid, SampleScene.Script);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SlotNotOnTheNode_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(SampleScene.Script, ScriptSlotId.New(), Jump)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing, SampleScene.Script);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodeWithoutScripting_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(SampleScene.B, SampleScene.MoveSlotId, Jump)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }
}
