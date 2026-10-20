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

        var result = await fixture.Authoring.ExecuteAsync(new AddScriptSlot(SampleScene.Script, added)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.SlotsOf(SampleScene.Script).Select(static slot => slot.Id).Should().Equal(SampleScene.MoveSlotId.Value, SampleScene.SecondSlotId.Value, added.Value);
        var slot = fixture.SlotOf(SampleScene.Script, added);
        _ = slot.Script.Should().BeNull();
        _ = slot.ExecutionOrder.Should().Be(0);
        _ = slot.Overrides.Should().BeEmpty();
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ScriptSlotsChanged(SampleScene.Script));
    }

    [TestMethod]
    public async Task Undo_RemovesTheSlot()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.SlotsOf(SampleScene.Script);
        _ = await fixture.Authoring.ExecuteAsync(new AddScriptSlot(SampleScene.Script, ScriptSlotId.New())).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(undone);
        _ = fixture.SlotsOf(SampleScene.Script).Should().Equal(before);
    }

    [TestMethod]
    public async Task SlotWithoutIdentity_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new AddScriptSlot(SampleScene.Script, ScriptSlotId.Empty)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing, SampleScene.Script);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task IdentityInUse_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new AddScriptSlot(SampleScene.Script, SampleScene.SecondSlotId)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryDuplicate, SampleScene.Script);
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
