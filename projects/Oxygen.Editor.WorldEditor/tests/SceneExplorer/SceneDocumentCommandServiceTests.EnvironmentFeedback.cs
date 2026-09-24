// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

/// <summary>Checks displayed environment values and sun dependencies through real authoring transitions.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task EnvironmentUndoRedo_RefreshesTheBoundInspectorValue()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        using var editor = new EnvironmentViewModel(fixture.Sut, () => context);
        editor.SetScene(scene);
        var original = editor.BackgroundR;
        editor.BeginEditSession("BackgroundR", NumberBoxEditInteractionKind.PointerDrag);
        editor.BackgroundR = 0.8f;
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await editor.PendingEdits.ConfigureAwait(false);

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = editor.BackgroundR.Should().Be(original);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = editor.BackgroundR.Should().Be(0.8f);
    }

    [TestMethod]
    public void EnvironmentSunOptions_TrackNodeAndComponentRemoval()
    {
        var scene = CreateScene();
        var sun = CreateDirectionalLightNode(scene, "Sun");
        scene.RootNodes.Add(sun);
        using var editor = new EnvironmentViewModel();
        editor.SetScene(scene);
        _ = editor.SelectedSun!.NodeId.Should().Be(sun.Id);

        _ = scene.RootNodes.Remove(sun);
        _ = editor.SelectedSun!.NodeId.Should().BeNull();
        scene.RootNodes.Add(sun);
        _ = editor.SelectedSun!.NodeId.Should().Be(sun.Id);
        var light = sun.Components.OfType<DirectionalLightComponent>().Single();
        _ = sun.RemoveComponent(light);
        _ = sun.AddComponent(light);
    }

    [TestMethod]
    public async Task EnvironmentDiagnosticBinding_RemainsStableWhenTheSceneArrives()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        using var editor = new EnvironmentViewModel(fixture.Sut, () => context);
        var boundDiagnostic = editor.ManualExposureEvDiagnostic;
        editor.SetScene(scene);

        editor.ManualExposureEv = float.NaN;
        await editor.PendingEdits.ConfigureAwait(false);

        _ = editor.ManualExposureEvDiagnostic.Should().BeSameAs(boundDiagnostic);
        _ = boundDiagnostic.Message.Should().NotBeEmpty();
        _ = float.IsFinite(editor.ManualExposureEv).Should().BeTrue();
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public void EnvironmentSceneSwitch_DetachesOldSunObservers()
    {
        var first = CreateScene();
        var firstSun = CreateDirectionalLightNode(first, "First Sun");
        first.RootNodes.Add(firstSun);
        var second = CreateScene();
        var secondSun = CreateDirectionalLightNode(second, "Second Sun");
        second.RootNodes.Add(secondSun);
        using var editor = new EnvironmentViewModel();
        editor.SetScene(first);
        editor.SetScene(second);
        var selected = editor.SelectedSun;

        first.RootNodes.Clear();
        firstSun.Name = "Old document change";

        _ = editor.SelectedSun.Should().BeSameAs(selected);
        _ = editor.SelectedSun!.NodeId.Should().Be(secondSun.Id);
        secondSun.Name = "Renamed Sun";
        _ = editor.SelectedSun.DisplayName.Should().Be("Renamed Sun");
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task UnrelatedEnvironmentEdit_PreservesTheAuthoredLightSunFlag(bool gesture)
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var sun = CreateDirectionalLightNode(scene, "Sun");
        scene.RootNodes.Add(sun);
        var light = sun.Components.OfType<DirectionalLightComponent>().Single();
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        using var editor = new EnvironmentViewModel(fixture.Sut, () => context);
        editor.SetScene(scene);
        if (gesture)
        {
            editor.BeginEditSession("BackgroundR", NumberBoxEditInteractionKind.PointerDrag);
        }

        editor.BackgroundR = 0.8f;
        if (gesture)
        {
            editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        }

        await editor.PendingEdits.ConfigureAwait(false);
        _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);
    }
}
