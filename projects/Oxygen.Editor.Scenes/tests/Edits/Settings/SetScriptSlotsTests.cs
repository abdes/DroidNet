// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Settings;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Settings;

/// <summary>
/// What is specific to <see cref="SetScriptSlots"/> (scene-edits.md 3.11 and 4): a node's script slots are replaced as
/// a whole, in order. Targets, locks, undo and the change stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetScriptSlotsTests
{
    [TestMethod]
    public async Task Slots_SetAndRaiseScriptSlotsChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var first = new ScriptSlot { Id = Guid.NewGuid(), Script = SampleScene.MoveScript, ExecutionOrder = 2 };
        var second = new ScriptSlot { Id = Guid.NewGuid(), Script = SampleScene.MoveScript, ExecutionOrder = 1 };

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptSlots(SampleScene.Script, [first, second])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.SlotsOf(SampleScene.Script).Should().Equal(first, second);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScriptSlotsChanged(SampleScene.Script));
    }

    [TestMethod]
    public async Task NodeWithoutScripting_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptSlots(SampleScene.B, [])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SlotWithoutIdentity_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptSlots(SampleScene.Script, [new ScriptSlot { Id = Guid.Empty, Script = SampleScene.MoveScript }])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing, SampleScene.Script);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task TwoSlotsOneIdentity_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var identity = Guid.NewGuid();
        var slots = ImmutableArray.Create(new ScriptSlot { Id = identity, Script = SampleScene.MoveScript }, new ScriptSlot { Id = identity, Script = SampleScene.MoveScript });
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptSlots(SampleScene.Script, slots)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryDuplicate, SampleScene.Script);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task ParameterWithoutName_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var unnamed = new ScriptSlot { Id = Guid.NewGuid(), Script = SampleScene.MoveScript }
            with
        { Overrides = ImmutableSortedDictionary.Create<string, ScriptValue>(StringComparer.Ordinal).Add(" ", ScriptValue.FromInt(1)) };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptSlots(SampleScene.Script, [unnamed])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.NameEmpty, SampleScene.Script);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task EqualSlots_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var current = fixture.SlotsOf(SampleScene.Script);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptSlots(SampleScene.Script, [.. current])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = current.Should().NotBeEmpty("the sample's Script has a slot");
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task ValueNamingADeletedAsset_IsKeptWithAWarning()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var surface = AssetPath.Parse("/Content/Surface.omat");
        fixture.Redirects.Delete(surface);
        var slot = new ScriptSlot { Id = Guid.NewGuid(), Script = SampleScene.MoveScript }
            with
        { Overrides = ImmutableSortedDictionary.Create<string, ScriptValue>(StringComparer.Ordinal).Add("surface", ScriptValue.FromAsset(surface)) };

        var result = await fixture.Authoring.ExecuteAsync(new SetScriptSlots(SampleScene.Script, [slot])).ConfigureAwait(false);

        SceneAssert.Warned(result, SceneEditCodes.AssetReferenceDeleted, SampleScene.Script);
        _ = fixture.SlotsOf(SampleScene.Script).Single().Overrides["surface"].Asset.Should().Be(surface);
    }
}
