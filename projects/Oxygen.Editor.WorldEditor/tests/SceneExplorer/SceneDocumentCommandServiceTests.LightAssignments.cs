// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task OccupiedAtmosphereSlotRejectsWholeCandidateWithoutDirtyHistoryOrSync()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var owner = CreateDirectionalLightNode(scene, "Stored primary");
        var candidate = CreateDirectionalLightNode(scene, "Candidate");
        var light = candidate.Components.OfType<DirectionalLightComponent>().Single();
        light.AtmosphereSlot = AtmosphereLightSlot.None;
        owner.Components.OfType<DirectionalLightComponent>().Single().AffectsWorld = false;
        scene.RootNodes.Add(owner);
        scene.RootNodes.Add(candidate);
        var before = light.Dehydrate();
        var context = CreateContext(scene);
        var edit = new PropertyEdit();
        edit.Set(SceneDocumentCommandService.DirectionalLight.IntensityLux, 123f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot, AtmosphereLightSlot.Primary);

        var result = await fixture.Sut.EditPropertiesAsync(context, [candidate.Id], edit,
            "Assign primary", EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = light.Dehydrate().Should().BeEquivalentTo(before);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        fixture.Sync.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task ClearingAssignmentIsUndoableAndPreservesOtherLightProperties()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var sun = CreateDirectionalLightNode(scene, "Sun");
        scene.RootNodes.Add(sun);
        var light = sun.Components.OfType<DirectionalLightComponent>().Single();
        light.UsePerPixelAtmosphereTransmittance = true;
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        using var editor = new DirectionalLightViewModel(fixture.Sut, () => context);
        editor.UpdateValues([sun]);
        editor.AtmosphereSlot = AtmosphereLightSlot.None;
        await editor.PendingEdits.ConfigureAwait(false);
        _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.None);
        _ = light.UsePerPixelAtmosphereTransmittance.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.None);
    }

    [TestMethod]
    public async Task EnvironmentPickerEditsCanonicalLightAssignmentAndTracksUndo()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var sun = CreateDirectionalLightNode(scene, "Sun");
        scene.RootNodes.Add(sun);
        var light = sun.Components.OfType<DirectionalLightComponent>().Single();
        light.AtmosphereSlot = AtmosphereLightSlot.None;
        var environment = scene.Environment;
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        using var editor = new EnvironmentViewModel(fixture.Sut, () => context);
        editor.SetScene(scene);
        editor.SelectedSun = editor.SunOptions.Single(option => option.NodeId == sun.Id);
        await editor.PendingEdits.ConfigureAwait(false);
        _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);
        _ = scene.Environment.Should().BeSameAs(environment);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.None);
        _ = editor.SelectedSun!.NodeId.Should().BeNull();
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = editor.SelectedSun!.NodeId.Should().Be(sun.Id);
    }

    [TestMethod]
    public async Task InvalidCoupledShadowEditLeavesAllFieldsAndHistoryUnchanged()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = CreateDirectionalLightNode(scene, "Sun");
        scene.RootNodes.Add(node);
        var light = node.Components.OfType<DirectionalLightComponent>().Single();
        var before = light.Dehydrate();
        var context = CreateContext(scene);
        var edit = new PropertyEdit();
        edit.Set(SceneDocumentCommandService.DirectionalLight.IntensityLux, 120f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.CascadeDistance0, 500f);
        var result = await fixture.Sut.EditPropertiesAsync(context, [node.Id], edit,
            "Invalid cascades", EditSessionToken.OneShot).ConfigureAwait(false);
        _ = result.Succeeded.Should().BeFalse();
        _ = light.Dehydrate().Should().BeEquivalentTo(before);
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.Metadata.IsDirty.Should().BeFalse();
    }
}
