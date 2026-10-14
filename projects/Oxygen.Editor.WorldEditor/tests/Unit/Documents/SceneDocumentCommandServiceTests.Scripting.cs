// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using AwesomeAssertions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Scripting;
using Oxygen.Editor.World.Serialization;
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
    public async Task DuplicateNodesInPlaceAsync_NodeValuesFollowTheCopiesAndKeepOtherNodes()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var (turret, barrel, player, enemy) = AddScriptedTurret(scene);
        var source = turret.Components.OfType<ScriptingComponent>().Single().Slots[0];
        var context = CreateContext(scene);

        var result = await fixture.Sut.DuplicateNodesInPlaceAsync(context, [turret.Id, enemy.Id]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var (turretCopy, enemyCopy) = (result.Value![0], result.Value[1]);
        var slot = turretCopy.Components.OfType<ScriptingComponent>().Single().Slots.Should().ContainSingle().Subject;
        _ = slot.Overrides["muzzle"].NodeId.Should().Be(turretCopy.Children.Single().Id, "the barrel was copied with its turret");
        _ = slot.Overrides["buddy"].NodeId.Should().Be(enemyCopy.Id, "the enemy was copied in the same duplication");
        _ = slot.Overrides["target"].NodeId.Should().Be(player.Id, "the player was not copied");
        _ = slot.Id.Should().Be(source.Id, "slot identities are scoped to their node");
        _ = turret.Components.OfType<ScriptingComponent>().Single().Slots[0].Should().Be(source);
        _ = barrel.Parent.Should().BeSameAs(turret);
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task DuplicateNodesAsync_NodeValuesFollowCopiesAcrossTheCopiedRoots()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var (turret, _, player, enemy) = AddScriptedTurret(scene);
        var context = CreateContext(scene);

        var result = await fixture.Sut.DuplicateNodesAsync(context, [turret.Id, enemy.Id], newParentNodeId: null, newParentFolderId: null).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var copy = result.Value!.Single(static node => string.Equals(node.Name, "Turret", StringComparison.Ordinal));
        var slot = copy.Components.OfType<ScriptingComponent>().Single().Slots[0];
        _ = slot.Overrides["muzzle"].NodeId.Should().Be(copy.Children.Single().Id);
        _ = slot.Overrides["buddy"].NodeId.Should().Be(result.Value!.Single(static node => string.Equals(node.Name, "Enemy", StringComparison.Ordinal)).Id);
        _ = slot.Overrides["target"].NodeId.Should().Be(player.Id);
    }

    [TestMethod]
    public async Task PasteExplorerItemsAsync_IntoAnotherSceneKeepsUncopiedNodeValuesAsMissing()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var origin = CreateScene();
        var (turret, _, player, _) = AddScriptedTurret(origin);
        origin.SetExplorerLayout([.. origin.RootNodes.Select(static node => new ExplorerEntryData { NodeId = node.Id })]);
        var payload = fixture.Sut.CaptureExplorerClipboard(CreateContext(origin), [turret.Id], []).Value!;
        var destination = CreateScene();
        destination.SetExplorerLayout([]);
        var context = CreateContext(destination);

        var result = await fixture.Sut.PasteExplorerItemsAsync(context, payload, false, null, null, false).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var copy = destination.RootNodes.Should().ContainSingle().Subject;
        var slot = copy.Components.OfType<ScriptingComponent>().Single().Slots[0];
        _ = slot.Overrides["muzzle"].NodeId.Should().Be(copy.Children.Single().Id);
        _ = slot.Overrides["target"].NodeId.Should().Be(player.Id, "a node outside the copy stays visible as missing");
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

    /// <summary>Adds a turret whose script points at its own barrel, a player and an enemy.</summary>
    private static (SceneNode turret, SceneNode barrel, SceneNode player, SceneNode enemy) AddScriptedTurret(Scene scene)
    {
        var turret = new SceneNode(scene) { Name = "Turret" };
        var barrel = new SceneNode(scene) { Name = "Barrel" };
        var player = new SceneNode(scene) { Name = "Player" };
        var enemy = new SceneNode(scene) { Name = "Enemy" };
        turret.AddChild(barrel);
        scene.RootNodes.Add(turret);
        scene.RootNodes.Add(player);
        scene.RootNodes.Add(enemy);
        var slot = new ScriptSlot { Script = MoveScript }
            .WithOverride("muzzle", ScriptValue.FromNode(barrel.Id))
            .WithOverride("target", ScriptValue.FromNode(player.Id))
            .WithOverride("buddy", ScriptValue.FromNode(enemy.Id));
        _ = turret.AddComponent(new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [slot] });
        return (turret, barrel, player, enemy);
    }
}
