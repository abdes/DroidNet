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
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Scripting;

/// <summary>
/// What is specific to <see cref="SetScriptValue"/>: a slot stores only the values that differ from the declared default,
/// a value must fit its parameter, and an asset value follows the asset. Targets, locks, undo and the change stream of
/// every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetScriptValueTests
{
    private static ScriptParameter Texture { get; } = new() { Name = "mask", Kind = ScriptParameterKind.AssetParameter, AssetType = (byte)AssetType.Material };

    private static AssetPath Mask { get; } = AssetPath.Parse("/Content/Mask.omat");

    [TestMethod]
    public async Task ValueDifferingFromTheDefault_IsStored()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotCases.First, ScriptSlotCases.Speed, ScriptValue.FromFloat(3f))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First).Overrides["speed"].FloatValue.Should().Be(3f);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScriptSlotsChanged(ScriptSlotCases.Node));
    }

    [TestMethod]
    public async Task ValueEqualToTheDefault_RemovesTheOverride()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotCases.First, ScriptSlotCases.Speed, ScriptValue.FromFloat(3f))).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotCases.First, ScriptSlotCases.Speed, ScriptValue.FromFloat(1f))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First).Overrides.Should().NotContainKey("speed");
    }

    [TestMethod]
    public async Task Null_RemovesTheOverride()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotCases.First, ScriptSlotCases.Target, Value: null)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First).Overrides.Should().BeEmpty();
    }

    [TestMethod]
    public async Task Undo_RestoresTheValue()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = ScriptSlotCases.SlotsOf(fixture);
        _ = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotCases.First, ScriptSlotCases.Target, ScriptValue.FromNode(SampleScene.A))).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = ScriptSlotCases.SlotsOf(fixture).Should().Equal(before);
    }

    [TestMethod]
    public async Task SameValue_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotCases.First, ScriptSlotCases.Target, ScriptValue.FromNode(SampleScene.B))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task ValueOfAnotherType_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotCases.First, ScriptSlotCases.Speed, ScriptValue.FromInt(3))).ConfigureAwait(false);

        SceneAssert.Invalid(result, ScriptDiagnosticCodes.SlotValueKindMismatch, ScriptSlotCases.Node);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task AssetOfAnotherType_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotCases.First, Texture, ScriptValue.FromAsset(SampleScene.MoveScript))).ConfigureAwait(false);

        SceneAssert.Invalid(result, ScriptDiagnosticCodes.SlotAssetTypeMismatch, ScriptSlotCases.Node);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task AssetThatMoved_IsFollowedToItsNewIdentity()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var moved = EditSamples.MovedTo(Mask);
        fixture.Redirects.Move(Mask, moved);

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotCases.First, Texture, ScriptValue.FromAsset(Mask))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First).Overrides["mask"].Asset.Should().Be(moved);
    }

    [TestMethod]
    public async Task AssetThatWasDeleted_IsKeptWithAWarning()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Redirects.Delete(Mask);

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotCases.First, Texture, ScriptValue.FromAsset(Mask))).ConfigureAwait(false);

        SceneAssert.Warned(result, SceneEditCodes.AssetReferenceDeleted, ScriptSlotCases.Node);
        _ = ScriptSlotCases.SlotOf(fixture, ScriptSlotCases.First).Overrides["mask"].Asset.Should().Be(Mask);
    }

    [TestMethod]
    public async Task ParameterWithoutName_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var unnamed = ScriptSlotCases.Speed with { Name = " " };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotCases.First, unnamed, ScriptValue.FromFloat(3f))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.NameEmpty, ScriptSlotCases.Node);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SlotNotOnTheNode_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptValue(ScriptSlotCases.Node, ScriptSlotId.New(), ScriptSlotCases.Speed, ScriptValue.FromFloat(3f))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing, ScriptSlotCases.Node);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodeWithoutScripting_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptValue(SampleScene.B, ScriptSlotCases.First, ScriptSlotCases.Speed, ScriptValue.FromFloat(3f))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }
}
