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
/// What is specific to <see cref="AddScriptSlot"/>: an empty slot is appended. Targets, locks, undo and the change stream
/// of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class AddScriptSlotTests
{
    [TestMethod]
    public async Task Slot_IsAppendedEmpty()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var added = ScriptSlotId.New();

        var result = await fixture.Authoring.ExecuteAsync(new AddScriptSlot(ScriptSlotCases.Node, added)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = ScriptSlotCases.IdsOf(fixture).Should().Equal(ScriptSlotCases.First.Value, ScriptSlotCases.Second.Value, added.Value);
        var slot = ScriptSlotCases.SlotOf(fixture, added);
        _ = slot.Script.Should().BeNull();
        _ = slot.ExecutionOrder.Should().Be(0);
        _ = slot.Overrides.Should().BeEmpty();
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScriptSlotsChanged(ScriptSlotCases.Node));
    }

    [TestMethod]
    public async Task Undo_RemovesTheSlot()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = ScriptSlotCases.SlotsOf(fixture);
        _ = await fixture.Authoring.ExecuteAsync(new AddScriptSlot(ScriptSlotCases.Node, ScriptSlotId.New())).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = ScriptSlotCases.SlotsOf(fixture).Should().Equal(before);
    }

    [TestMethod]
    public async Task SlotWithoutIdentity_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new AddScriptSlot(ScriptSlotCases.Node, ScriptSlotId.Empty)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing, ScriptSlotCases.Node);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task IdentityInUse_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new AddScriptSlot(ScriptSlotCases.Node, ScriptSlotCases.Second)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryDuplicate, ScriptSlotCases.Node);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodeWithoutScripting_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new AddScriptSlot(SampleScene.B, ScriptSlotId.New())).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }
}
