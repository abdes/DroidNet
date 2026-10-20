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
        var before = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First);

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(ScriptSlotCases.Node, ScriptSlotCases.First, Jump)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var slot = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First);
        _ = slot.Script.Should().Be(Jump);
        _ = slot.Overrides.Should().ContainKey("target", "a value the new script does not declare becomes stale, not lost");
        _ = slot.Should().Be(before with { Script = Jump });
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScriptSlotsChanged(ScriptSlotCases.Node));
    }

    [TestMethod]
    public async Task Null_ClearsTheScript()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(ScriptSlotCases.Node, ScriptSlotCases.First, Script: null)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First).Script.Should().BeNull();
    }

    [TestMethod]
    public async Task Undo_RestoresTheScript()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = ScriptSlotCases.SlotsOf(fixture);
        _ = await fixture.Authoring.ExecuteAsync(new SetSlotScript(ScriptSlotCases.Node, ScriptSlotCases.First, Jump)).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = ScriptSlotCases.SlotsOf(fixture).Should().Equal(before);
    }

    [TestMethod]
    public async Task SameScript_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(ScriptSlotCases.Node, ScriptSlotCases.First, SampleScene.MoveScript)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task ScriptThatMoved_IsFollowedToItsNewIdentity()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var moved = EditSamples.MovedTo(Jump);
        fixture.Redirects.Move(Jump, moved);

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(ScriptSlotCases.Node, ScriptSlotCases.First, Jump)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First).Script.Should().Be(moved);
    }

    [TestMethod]
    public async Task ScriptThatWasDeleted_IsKeptWithAWarning()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Redirects.Delete(Jump);

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(ScriptSlotCases.Node, ScriptSlotCases.First, Jump)).ConfigureAwait(false);

        SceneAssert.Warned(result, SceneEditCodes.AssetReferenceDeleted, ScriptSlotCases.Node);
        _ = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First).Script.Should().Be(Jump);
    }

    [TestMethod]
    public async Task AssetThatIsNotAScript_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(ScriptSlotCases.Node, ScriptSlotCases.First, SampleScene.RedMaterial)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AssetReferenceInvalid, ScriptSlotCases.Node);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SlotNotOnTheNode_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(ScriptSlotCases.Node, ScriptSlotId.New(), Jump)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing, ScriptSlotCases.Node);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodeWithoutScripting_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSlotScript(SampleScene.B, ScriptSlotCases.First, Jump)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }
}
