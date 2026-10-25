// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using DroidNet.Controls;
using Oxygen.Editor.Input;
using Oxygen.Editor.SceneInspector.Components.Cameras;
using Oxygen.Editor.SceneInspector.Components.Lights;
using Oxygen.Editor.SceneInspector.SceneSettings;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Unit.Tests;

/// <summary>
/// Checks that the Inspector's component, environment and Play models turn what people type and drag into the scene
/// document's edits: one undo step per gesture, nothing left after a cancel, and values that follow undo and redo. What
/// an edit does to the scene is the Scenes module's to test.
/// </summary>
[TestClass]
[TestCategory("Inspector")]
public sealed class InspectorSceneEditingTests
{
    private static readonly AssetPath GameplayContext = AssetPath.Parse("/Content/Input/Gameplay.oimap");
    private static readonly AssetPath MenuContext = AssetPath.Parse("/Content/Input/Menu.oimap");

    [TestMethod]
    public async Task CameraDrag_OverTwoCameras_IsOneUndoStepThatRestoresEach()
    {
        var scene = CreateScene();
        using var testScene = Open(scene, CameraNode(scene, 60), CameraNode(scene, 80), out var first, out var second);
        using var editor = new PerspectiveCameraViewModel(() => testScene.Authoring);
        editor.UpdateValues([first, second]);
        editor.BeginEditSession("FieldOfView", NumberBoxEditInteractionKind.PointerDrag);

        for (var value = 1; value <= 100; ++value)
        {
            editor.FieldOfView.Value = value;
        }

        _ = testScene.Document.History.UndoStack.Should().BeEmpty();
        editor.CompleteEditSession(new NumberBoxEditSessionEventArgs(NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit));
        await editor.PendingEdits.ConfigureAwait(false);

        _ = testScene.Document.History.UndoStack.Should().ContainSingle();
        _ = FieldOfViewOf(first).Should().Be(100);
        _ = await testScene.Authoring.UndoAsync().ConfigureAwait(false);
        _ = (FieldOfViewOf(first), FieldOfViewOf(second)).Should().Be((60f, 80f));
        _ = await testScene.Authoring.RedoAsync().ConfigureAwait(false);
        _ = FieldOfViewOf(second).Should().Be(100);
    }

    [TestMethod]
    public async Task CameraDrag_CancelledAfterTheSelectionChanged_LeavesBothCameras()
    {
        var scene = CreateScene();
        using var testScene = Open(scene, CameraNode(scene, 60), CameraNode(scene, 80), out var first, out var second);
        using var editor = new PerspectiveCameraViewModel(() => testScene.Authoring);
        editor.UpdateValues([first]);
        editor.BeginEditSession("FieldOfView", NumberBoxEditInteractionKind.PointerDrag);
        editor.FieldOfView.Value = 110;

        editor.UpdateValues([second]);
        editor.FieldOfView.Value = 120;
        editor.CompleteEditSession(new NumberBoxEditSessionEventArgs(NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Cancel));
        await editor.PendingEdits.ConfigureAwait(false);

        _ = (FieldOfViewOf(first), FieldOfViewOf(second)).Should().Be((60f, 80f));
        _ = editor.FieldOfView.Value.Should().Be(80);
        _ = testScene.Document.History.UndoStack.Should().BeEmpty();
        _ = testScene.Document.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task WheelTicks_CommitOnceAfterTheWheelIdles()
    {
        var scene = CreateScene();
        using var testScene = Open(scene, CameraNode(scene, 60), out var node);
        using var editor = new PerspectiveCameraViewModel(() => testScene.Authoring);
        editor.UpdateValues([node]);
        for (var tick = 0; tick < 4; ++tick)
        {
            editor.BeginEditSession("FieldOfView", NumberBoxEditInteractionKind.MouseWheel);
            editor.FieldOfView.Value = 61 + tick;
            editor.CompleteEditSession(new NumberBoxEditSessionEventArgs(NumberBoxEditInteractionKind.MouseWheel, NumberBoxEditCompletionKind.Commit));
        }

        _ = testScene.Document.History.UndoStack.Should().BeEmpty();
        await editor.PendingEdits.ConfigureAwait(false);

        _ = testScene.Document.History.UndoStack.Should().ContainSingle();
        _ = FieldOfViewOf(node).Should().Be(64);
    }

    [TestMethod]
    public async Task RefusedCameraValue_ShowsTheSceneValueAndDiagnosesBothFieldsUntilFixed()
    {
        var scene = CreateScene();
        var built = CameraNode(scene, 60);
        var builtCamera = built.Components.OfType<PerspectiveCamera>().Single();
        builtCamera.NearPlane = 1;
        builtCamera.FarPlane = 10;
        using var testScene = Open(scene, built, out var node);
        var camera = node.Components.OfType<PerspectiveCamera>().Single();
        using var editor = new PerspectiveCameraViewModel(() => testScene.Authoring);
        editor.UpdateValues([node]);
        editor.BeginEditSession("NearPlane", NumberBoxEditInteractionKind.Text);
        editor.NearPlane.Value = 11;
        await editor.PendingEdits.ConfigureAwait(false);

        _ = (camera.NearPlane, editor.NearPlane.Value).Should().Be((1f, 1f));
        _ = editor.NearPlaneDiagnostic.Message.Should().NotBeEmpty();
        _ = editor.FarPlaneDiagnostic.Message.Should().NotBeEmpty();
        _ = testScene.Document.History.UndoStack.Should().BeEmpty();
        editor.EndEditSession(NumberBoxEditCompletionKind.Cancel);
        await editor.PendingEdits.ConfigureAwait(false);
        editor.BeginEditSession("FarPlane", NumberBoxEditInteractionKind.Text);
        editor.FarPlane.Value = 20;
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await editor.PendingEdits.ConfigureAwait(false);

        _ = editor.NearPlaneDiagnostic.Message.Should().BeEmpty();
        _ = editor.FarPlaneDiagnostic.Message.Should().BeEmpty();
    }

    [TestMethod]
    public async Task FieldOfViewAboveItsRange_CommitsTheClampedValue()
    {
        var scene = CreateScene();
        using var testScene = Open(scene, CameraNode(scene, 60), out var node);
        using var editor = new PerspectiveCameraViewModel(() => testScene.Authoring);
        editor.UpdateValues([node]);
        editor.BeginEditSession("FieldOfView", NumberBoxEditInteractionKind.Text);
        editor.FieldOfView.Value = 200;
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await editor.PendingEdits.ConfigureAwait(false);

        _ = FieldOfViewOf(node).Should().Be(179f);
        _ = editor.FieldOfView.Value.Should().Be(179);
        _ = editor.FieldOfViewDiagnostic.Code.Should().BeEmpty();
        _ = testScene.Document.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task LightColorChannelAboveOne_CommitsOne()
    {
        var scene = CreateScene();
        var built = DirectionalLightNode(scene, "Sun", AtmosphereLightSlot.None);
        built.Components.OfType<DirectionalLightComponent>().Single().Color = new Vector3(0.1f, 0.2f, 0.3f);
        using var testScene = Open(scene, built, out var node);
        using var editor = new DirectionalLightViewModel(() => testScene.Authoring);
        editor.UpdateValues([node]);
        editor.BeginEditSession("ColorR", NumberBoxEditInteractionKind.Text);
        editor.ColorR = 1.5f;
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await editor.PendingEdits.ConfigureAwait(false);

        _ = ColorOf(node).Should().Be(new Vector3(1f, 0.2f, 0.3f));
        _ = testScene.Document.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task NearPlaneAtZero_IsRefusedNotClamped()
    {
        var scene = CreateScene();
        using var testScene = Open(scene, CameraNode(scene, 60), out var node);
        var camera = node.Components.OfType<PerspectiveCamera>().Single();
        var near = camera.NearPlane;
        using var editor = new PerspectiveCameraViewModel(() => testScene.Authoring);
        editor.UpdateValues([node]);
        editor.BeginEditSession("NearPlane", NumberBoxEditInteractionKind.Text);
        editor.NearPlane.Value = 0;
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await editor.PendingEdits.ConfigureAwait(false);

        _ = camera.NearPlane.Should().Be(near);
        _ = editor.NearPlaneDiagnostic.Code.Should().Be(SceneEditCodes.PropertyOutOfRange);
        _ = testScene.Document.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task LightColorChannel_KeepsEachLightsOtherChannelsInOneUndoStep()
    {
        var scene = CreateScene();
        var firstBuilt = DirectionalLightNode(scene, "First", AtmosphereLightSlot.None);
        var secondBuilt = DirectionalLightNode(scene, "Second", AtmosphereLightSlot.None);
        firstBuilt.Components.OfType<DirectionalLightComponent>().Single().Color = new Vector3(0.1f, 0.2f, 0.3f);
        secondBuilt.Components.OfType<DirectionalLightComponent>().Single().Color = new Vector3(0.4f, 0.5f, 0.6f);
        using var testScene = Open(scene, firstBuilt, secondBuilt, out var first, out var second);
        using var editor = new DirectionalLightViewModel(() => testScene.Authoring);
        editor.UpdateValues([first, second]);
        editor.BeginEditSession("ColorR", NumberBoxEditInteractionKind.PointerDrag);
        editor.ColorR = 0.7f;
        editor.ColorR = 0.8f;
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await editor.PendingEdits.ConfigureAwait(false);

        _ = ColorOf(first).Should().Be(new Vector3(0.8f, 0.2f, 0.3f));
        _ = ColorOf(second).Should().Be(new Vector3(0.8f, 0.5f, 0.6f));
        _ = testScene.Document.History.UndoStack.Should().ContainSingle();
        _ = await testScene.Authoring.UndoAsync().ConfigureAwait(false);
        _ = ColorOf(first).Should().Be(new Vector3(0.1f, 0.2f, 0.3f));
        _ = ColorOf(second).Should().Be(new Vector3(0.4f, 0.5f, 0.6f));
    }

    [TestMethod]
    public async Task EnvironmentDrag_IsOneUndoStepAndTheFieldFollowsUndoAndRedo()
    {
        using var testScene = TestScenes.Open(CreateScene());
        using var editor = new SceneSettingsViewModel(() => testScene.Authoring);
        editor.SetScene(testScene.Scene);
        var original = editor.Backdrop.SolidColorR;
        editor.BeginEditSession("BackgroundR", NumberBoxEditInteractionKind.PointerDrag);
        for (var sample = 1; sample <= 100; ++sample)
        {
            editor.Backdrop.SolidColorR = sample / 100f;
        }

        _ = testScene.Document.History.UndoStack.Should().BeEmpty();
        editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await editor.PendingEdits.ConfigureAwait(false);
        _ = testScene.Document.History.UndoStack.Should().ContainSingle();

        _ = await testScene.Authoring.UndoAsync().ConfigureAwait(false);
        _ = editor.Backdrop.SolidColorR.Should().Be(original);
        _ = await testScene.Authoring.RedoAsync().ConfigureAwait(false);
        _ = editor.Backdrop.SolidColorR.Should().Be(1f);
    }

    [TestMethod]
    public async Task EnvironmentDrag_PreviewsThenCancelsWithoutHistory()
    {
        using var testScene = TestScenes.Open(CreateScene());
        var original = SceneSkyFields.SolidColorOf(testScene.Scene.Environment);
        using var editor = new SceneSettingsViewModel(() => testScene.Authoring);
        editor.SetScene(testScene.Scene);
        editor.BeginEditSession("BackgroundR", NumberBoxEditInteractionKind.PointerDrag);
        editor.Backdrop.SolidColorR = 0.8f;
        _ = SceneSkyFields.SolidColorOf(testScene.Scene.Environment).X.Should().Be(0.8f);

        editor.EndEditSession(NumberBoxEditCompletionKind.Cancel);
        await editor.PendingEdits.ConfigureAwait(false);

        _ = SceneSkyFields.SolidColorOf(testScene.Scene.Environment).Should().Be(original);
        _ = testScene.Document.History.UndoStack.Should().BeEmpty();
        _ = testScene.Document.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    [DataRow(false, DisplayName = "Typed")]
    [DataRow(true, DisplayName = "Dragged")]
    public async Task UnrelatedEnvironmentEdit_KeepsTheSunsAtmosphereSlot(bool gesture)
    {
        var scene = CreateScene();
        using var testScene = Open(scene, DirectionalLightNode(scene, "Sun", AtmosphereLightSlot.Primary), out var sun);
        var light = sun.Components.OfType<DirectionalLightComponent>().Single();
        using var editor = new SceneSettingsViewModel(() => testScene.Authoring);
        editor.SetScene(testScene.Scene);
        if (gesture)
        {
            editor.BeginEditSession("BackgroundR", NumberBoxEditInteractionKind.PointerDrag);
        }

        editor.Backdrop.SolidColorR = 0.8f;
        if (gesture)
        {
            editor.EndEditSession(NumberBoxEditCompletionKind.Commit);
        }

        await editor.PendingEdits.ConfigureAwait(false);
        _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);
        _ = await testScene.Authoring.UndoAsync().ConfigureAwait(false);
        _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);
        _ = await testScene.Authoring.RedoAsync().ConfigureAwait(false);
        _ = light.AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);
    }

    [TestMethod]
    public async Task EnvironmentDiagnostic_KeepsTheBoundInstanceWhenTheSceneArrives()
    {
        using var testScene = TestScenes.Open(CreateScene());
        using var editor = new SceneSettingsViewModel(() => testScene.Authoring);
        var boundDiagnostic = editor.Exposure.ManualExposureEvDiagnostic;
        editor.SetScene(testScene.Scene);

        editor.Exposure.ManualExposureEv = double.NaN;
        await editor.PendingEdits.ConfigureAwait(false);

        _ = editor.Exposure.ManualExposureEvDiagnostic.Should().BeSameAs(boundDiagnostic);
        _ = boundDiagnostic.Message.Should().NotBeEmpty();
        _ = double.IsFinite(editor.Exposure.ManualExposureEv).Should().BeTrue();
        _ = testScene.Document.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public void EnvironmentSunChoice_FollowsTheSunNodeAndComponent()
    {
        var scene = CreateScene();
        var sun = DirectionalLightNode(scene, "Sun", AtmosphereLightSlot.Primary);
        scene.RootNodes.Add(sun);
        using var editor = new SceneSettingsViewModel();
        editor.SetScene(scene);
        _ = editor.AtmosphereLights.SelectedSun!.NodeId.Should().Be(sun.Id);

        _ = scene.RootNodes.Remove(sun);
        _ = editor.AtmosphereLights.SelectedSun!.NodeId.Should().BeNull();
        scene.RootNodes.Add(sun);
        _ = editor.AtmosphereLights.SelectedSun!.NodeId.Should().Be(sun.Id);
    }

    [TestMethod]
    public void EnvironmentSceneSwitch_StopsObservingTheOldSun()
    {
        var first = CreateScene();
        var firstSun = DirectionalLightNode(first, "First Sun", AtmosphereLightSlot.Primary);
        first.RootNodes.Add(firstSun);
        var second = CreateScene();
        var secondSun = DirectionalLightNode(second, "Second Sun", AtmosphereLightSlot.Primary);
        second.RootNodes.Add(secondSun);
        using var editor = new SceneSettingsViewModel();
        editor.SetScene(first);
        editor.SetScene(second);
        var selected = editor.AtmosphereLights.SelectedSun;

        first.RootNodes.Clear();
        firstSun.Metadata.Name = "Old document change";

        _ = editor.AtmosphereLights.SelectedSun.Should().BeSameAs(selected);
        _ = editor.AtmosphereLights.SelectedSun!.NodeId.Should().Be(secondSun.Id);
        secondSun.Metadata.Name = "Renamed Sun";
        _ = editor.AtmosphereLights.SelectedSun.DisplayName.Should().Be("Renamed Sun");
    }

    [TestMethod]
    public async Task PlaySection_NamesTheAutomaticCameraAndAuthorsTheChosenOne()
    {
        var scene = CreateScene();
        var firstBuilt = CameraNode(scene, 60);
        firstBuilt.Metadata.Name = "Overview";
        var secondBuilt = CameraNode(scene, 60);
        secondBuilt.Metadata.Name = "Gameplay";
        using var testScene = Open(scene, firstBuilt, secondBuilt, out _, out var second);
        using var editor = new SceneSettingsViewModel(() => testScene.Authoring);
        editor.SetScene(testScene.Scene);
        var play = editor.Play;

        _ = play.CameraChoices.Select(static choice => choice.Name).Should().Equal("Automatic", "Overview", "Gameplay");
        _ = play.SelectedCamera!.Id.Should().BeNull();
        _ = (play.ShowsAutomaticCamera, play.AutomaticCameraName).Should().Be((true, "Overview"));

        play.SelectedCamera = play.CameraChoices.Single(choice => choice.Id == NodeReference.To(second.Id));
        await play.Pending.ConfigureAwait(false);

        _ = testScene.Scene.StartingCamera.Should().Be(NodeReference.To(second.Id));
        _ = play.ShowsAutomaticCamera.Should().BeFalse("a chosen camera needs no explanation");
        _ = await testScene.Authoring.UndoAsync().ConfigureAwait(false);
        _ = play.SelectedCamera!.Id.Should().BeNull();
    }

    [TestMethod]
    public async Task PlaySection_KeepsItsCameraListAndSelectionAcrossRefreshes()
    {
        var scene = CreateScene();
        using var testScene = Open(scene, CameraNode(scene, 60), CameraNode(scene, 60), out _, out var second);
        using var editor = new SceneSettingsViewModel(() => testScene.Authoring);
        editor.SetScene(testScene.Scene);
        var play = editor.Play;
        play.SelectedCamera = play.CameraChoices.Single(choice => choice.Id == NodeReference.To(second.Id));
        await play.Pending.ConfigureAwait(false);
        var entries = play.CameraChoices.ToArray();
        var selected = play.SelectedCamera;

        // An edit's completion, the catalog and input enabling all refresh the section.
        play.Refresh();
        play.SetInputEnabled(enabled: true);

        _ = play.CameraChoices.Should().Equal(entries, "an open list and its selection survive refreshes that change no camera");
        _ = play.SelectedCamera.Should().BeSameAs(selected);

        second.Metadata.Name = "Chase cam";
        play.Refresh();

        _ = play.CameraChoices.Select(static choice => choice.Name).Should().Contain("Chase cam");
        _ = play.SelectedCamera!.Id.Should().Be(second.Id);
        _ = play.SelectedCamera.Name.Should().Be("Chase cam");
    }

    [TestMethod]
    public void PlaySection_ShowsADeletedStartingCameraAsMissing()
    {
        var scene = CreateScene();
        var camera = CameraNode(scene, 60);
        scene.RootNodes.Add(camera);
        scene.SetStartingCamera(NodeReference.To(camera.Id));
        _ = scene.RootNodes.Remove(camera);
        using var editor = new SceneSettingsViewModel();

        editor.SetScene(scene);

        _ = editor.Play.SelectedCamera!.Name.Should().Be("Missing camera");
        _ = editor.Play.HasCameraError.Should().BeTrue();
        _ = editor.Play.CameraChoices[0].Name.Should().Be("Automatic");
        _ = (editor.Play.ShowsAutomaticCamera, editor.Play.ShowsNoCamera).Should().Be((false, false), "the error explains the start instead");
    }

    [TestMethod]
    public async Task PlaySection_PriorityShowsTheContextsOwnUntilOverriddenAndRowsFollowThePriority()
    {
        var scene = CreateScene();
        scene.SetStartingInputContexts([new StartingInputContextData(GameplayContext), new StartingInputContextData(MenuContext)]);
        using var testScene = TestScenes.Open(scene);
        using var editor = new SceneSettingsViewModel(() => testScene.Authoring, inputContextSource: uri => uri == GameplayContext ? new InputContextSource(IsProjectContext: true, DeclaredPriority: 3) : InputContextSource.External);
        editor.SetScene(testScene.Scene);
        var gameplay = editor.Play.Contexts[0];

        _ = (gameplay.Priority, gameplay.IsOverridden, editor.Play.Contexts[1].Priority).Should().Be((3.0, false, 0.0));
        _ = gameplay.ResetToolTip.Should().Be("Use the context's own priority (3)");

        gameplay.Priority = 8;
        await editor.Play.Pending.ConfigureAwait(false);
        _ = testScene.Scene.StartingInputContexts[0].Priority.Should().Be(8);
        _ = gameplay.IsOverridden.Should().BeTrue();

        gameplay.Priority = 3;
        await editor.Play.Pending.ConfigureAwait(false);
        _ = testScene.Scene.StartingInputContexts[0].Priority.Should().BeNull("typing the context's own priority follows it again");

        var menu = editor.Play.Contexts[1];
        menu.Priority = 5;
        await editor.Play.Pending.ConfigureAwait(false);
        _ = editor.Play.Contexts.Select(static row => row.Context).Should().Equal([MenuContext, GameplayContext], "the highest priority is evaluated first");
    }

    [TestMethod]
    public void PlaySection_AContextNoInputFileDeclaresIsMissingAndOnlyRemovable()
    {
        var scene = CreateScene();
        scene.SetStartingInputContexts([new StartingInputContextData(GameplayContext), new StartingInputContextData(MenuContext)]);
        using var editor = new SceneSettingsViewModel(
            inputContextSource: uri => uri == GameplayContext
                ? new InputContextSource(IsProjectContext: true, DeclaredPriority: null)
                : new InputContextSource(IsProjectContext: true, DeclaredPriority: 0));

        editor.SetScene(scene);

        var gone = editor.Play.Contexts.Single(row => row.Context == GameplayContext);
        _ = (gone.IsMissing, gone.CanReveal, gone.RevealCommand.CanExecute(parameter: null)).Should().Be((true, false, false), "its input file was deleted, whatever the last cook left");
        _ = editor.Play.Contexts.Single(row => row.Context == MenuContext).IsMissing.Should().BeFalse();
    }

    [TestMethod]
    public async Task StartingCameraToggle_TakesTheStartFromAnotherCameraAndReturnsToAutomatic()
    {
        var scene = CreateScene();
        using var testScene = Open(scene, CameraNode(scene, 60), CameraNode(scene, 60), out var first, out var second);
        using var firstToggle = new StartingCameraToggle(() => testScene.Authoring);
        using var secondToggle = new StartingCameraToggle(() => testScene.Authoring);
        firstToggle.Bind([first]);
        secondToggle.Bind([second]);

        _ = (firstToggle.IsChecked, firstToggle.IsAutomatic, secondToggle.IsAutomatic).Should().Be((false, true, false));

        secondToggle.IsChecked = true;
        await secondToggle.Pending.ConfigureAwait(false);
        _ = testScene.Scene.StartingCamera.Should().Be(NodeReference.To(second.Id));
        _ = (firstToggle.IsAutomatic, secondToggle.IsChecked).Should().Be((false, true));

        firstToggle.IsChecked = true;
        await firstToggle.Pending.ConfigureAwait(false);
        _ = testScene.Scene.StartingCamera.Should().Be(NodeReference.To(first.Id));
        _ = (firstToggle.IsChecked, secondToggle.IsChecked).Should().Be((true, false));

        firstToggle.IsChecked = false;
        await firstToggle.Pending.ConfigureAwait(false);
        _ = testScene.Scene.StartingCamera.Should().BeNull();
        _ = firstToggle.IsAutomatic.Should().BeTrue();
    }

    private static Scene CreateScene()
    {
        return new Scene { Name = "Inspector Scene" };
    }

    private static SceneNode CameraNode(Scene scene, float fieldOfView)
    {
        var node = new SceneNode(scene, "Camera");
        node.Components.Add(new PerspectiveCamera { Name = "Camera", FieldOfView = fieldOfView });
        return node;
    }

    private static SceneNode DirectionalLightNode(Scene scene, string name, AtmosphereLightSlot slot)
    {
        var node = new SceneNode(scene, name);
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Directional Light", AtmosphereSlot = slot });
        return node;
    }

    // The caller disposes the opened scene.
    private static TestScene Open(Scene scene, SceneNode node, out SceneNode opened)
    {
        scene.RootNodes.Add(node);
        var testScene = TestScenes.Open(scene);
        opened = testScene.NodeOf(node);
        return testScene;
    }

    // The caller disposes the opened scene.
    private static TestScene Open(Scene scene, SceneNode first, SceneNode second, out SceneNode openedFirst, out SceneNode openedSecond)
    {
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var testScene = TestScenes.Open(scene);
        openedFirst = testScene.NodeOf(first);
        openedSecond = testScene.NodeOf(second);
        return testScene;
    }

    private static float FieldOfViewOf(SceneNode node) => node.Components.OfType<PerspectiveCamera>().Single().FieldOfView;

    private static Vector3 ColorOf(SceneNode node) => node.Components.OfType<DirectionalLightComponent>().Single().Color;
}
