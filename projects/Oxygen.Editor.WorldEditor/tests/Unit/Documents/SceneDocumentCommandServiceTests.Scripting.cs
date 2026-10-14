// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using AwesomeAssertions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Scripting;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Checks script slot edits: adding the component, slot changes, validation, history and dirty state.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    private static readonly Uri MoveScript = new("asset:///Content/Player/move.oscript");

    [TestMethod]
    public async Task AddComponentAsync_Scripting_StartsWithOneEmptySlotAndOnlyOnce()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Player" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        var added = await fixture.Sut.AddComponentAsync(context, node.Id, typeof(ScriptingComponent)).ConfigureAwait(false);
        var again = await fixture.Sut.AddComponentAsync(context, node.Id, typeof(ScriptingComponent)).ConfigureAwait(false);

        _ = added.Succeeded.Should().BeTrue();
        _ = added.Value.Should().BeOfType<ScriptingComponent>().Which.Slots.Should().ContainSingle().Which.Script.Should().BeNull();
        _ = again.Succeeded.Should().BeFalse();
        _ = node.Components.OfType<ScriptingComponent>().Should().ContainSingle();
    }

    [TestMethod]
    public async Task EditScriptSlotsAsync_UndoRedoRestoresTheSlots()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Player" };
        scene.RootNodes.Add(node);
        var scripting = new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [new ScriptSlot()] };
        _ = node.AddComponent(scripting);
        var context = CreateContext(scene);
        var before = scripting.Slots;
        var slot = before[0].Id;
        var after = ScriptSlotEdits.SetExecutionOrder(ScriptSlotEdits.SetScript(ScriptSlotEdits.Add(before), slot, MoveScript), slot, -3);

        var result = await fixture.Sut.EditScriptSlotsAsync(context, node.Id, after).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scripting.Slots.Should().Equal(after);
        _ = scripting.Slots[0].Script.Should().Be(MoveScript);
        _ = scripting.Slots[0].ExecutionOrder.Should().Be(-3);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scripting.Slots.Should().Equal(before);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scripting.Slots.Should().Equal(after);
    }

    [TestMethod]
    public async Task EditScriptSlotsAsync_UnchangedSlotsAddNoHistory()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Player" };
        scene.RootNodes.Add(node);
        var scripting = new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [new ScriptSlot { Script = MoveScript }] };
        _ = node.AddComponent(scripting);
        var context = CreateContext(scene);

        var result = await fixture.Sut.EditScriptSlotsAsync(context, node.Id, [.. scripting.Slots]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.Metadata.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public void Retarget_RepointsSlotScriptsAndAssetValues()
    {
        var jump = new Uri("asset:///Content/Input/Jump.oiact");
        var walk = new Uri("asset:///Content/Player/walk.oscript");
        var leap = new Uri("asset:///Content/Input/Leap.oiact");
        ImmutableArray<ScriptSlot> slots = [new ScriptSlot { Script = MoveScript }
            .WithOverride("jump", ScriptValue.FromAsset(jump))
            .WithOverride("speed", ScriptValue.FromFloat(2f))];

        var moved = SceneReferenceRetargeting.Retarget(slots, uri => uri == MoveScript ? walk : uri == jump ? leap : null);

        _ = moved.Should().NotBeNull();
        _ = moved!.Value[0].Script.Should().Be(walk);
        _ = moved.Value[0].Overrides["jump"].AssetUri.Should().Be(leap);
        _ = moved.Value[0].Overrides["speed"].FloatValue.Should().Be(2f);
        _ = SceneReferenceRetargeting.Retarget(slots, static _ => null).Should().BeNull();
    }

    [TestMethod]
    public async Task EditScriptSlotsAsync_RejectsDuplicateSlotsAndNonScriptReferences()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Player" };
        scene.RootNodes.Add(node);
        var scripting = new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [new ScriptSlot()] };
        _ = node.AddComponent(scripting);
        var context = CreateContext(scene);
        var duplicate = scripting.Slots[0];

        var twice = await fixture.Sut.EditScriptSlotsAsync(context, node.Id, [duplicate, duplicate]).ConfigureAwait(false);
        var material = await fixture.Sut.EditScriptSlotsAsync(context, node.Id, [duplicate with { Script = new Uri("asset:///Content/Materials/Red.omat") }]).ConfigureAwait(false);

        _ = twice.Succeeded.Should().BeFalse();
        _ = material.Succeeded.Should().BeFalse();
        _ = scripting.Slots.Should().Equal(duplicate);
        _ = context.History.UndoStack.Should().BeEmpty();
    }
}
