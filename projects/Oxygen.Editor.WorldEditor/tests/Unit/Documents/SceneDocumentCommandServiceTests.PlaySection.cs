// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Checks the scene's Play section and the cameras' Starting camera toggle through real scene commands.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task PlaySection_NamesTheAutomaticCameraAndAuthorsTheChosenOne()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var first = CameraNode(scene, fieldOfView: 60f);
        first.Name = "Overview";
        var second = CameraNode(scene, fieldOfView: 60f);
        second.Name = "Gameplay";
        var context = CreateContext(scene);
        using var editor = new EnvironmentViewModel(fixture.Sut, () => context);
        editor.SetScene(scene);
        var play = editor.Play;

        _ = play.CameraChoices.Select(static choice => choice.Name).Should().Equal("Automatic", "Overview", "Gameplay");
        _ = play.SelectedCamera!.Id.Should().BeNull();
        _ = (play.ShowsAutomaticCamera, play.AutomaticCameraName).Should().Be((true, "Overview"));

        play.SelectedCamera = play.CameraChoices.Single(choice => choice.Id == second.Id);
        await play.Pending.ConfigureAwait(false);

        _ = scene.Play.StartingCamera.Should().Be(second.Id);
        _ = play.ShowsAutomaticCamera.Should().BeFalse("a chosen camera needs no explanation");
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = play.SelectedCamera!.Id.Should().BeNull();
    }

    [TestMethod]
    public async Task PlaySection_KeepsItsCameraListAndSelectionAcrossRefreshes()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        _ = CameraNode(scene, fieldOfView: 60f);
        var second = CameraNode(scene, fieldOfView: 60f);
        var context = CreateContext(scene);
        using var editor = new EnvironmentViewModel(fixture.Sut, () => context);
        editor.SetScene(scene);
        var play = editor.Play;
        play.SelectedCamera = play.CameraChoices.Single(choice => choice.Id == second.Id);
        await play.Pending.ConfigureAwait(false);
        var entries = play.CameraChoices.ToArray();
        var selected = play.SelectedCamera;

        // An edit's completion, the catalog and input enabling all refresh the section.
        play.Refresh();
        play.SetInputEnabled(enabled: true);

        _ = play.CameraChoices.Should().Equal(entries, "an open list and its selection survive refreshes that change no camera");
        _ = play.SelectedCamera.Should().BeSameAs(selected);

        second.Name = "Chase cam";
        play.Refresh();

        _ = play.CameraChoices.Select(static choice => choice.Name).Should().Contain("Chase cam");
        _ = play.SelectedCamera!.Id.Should().Be(second.Id);
        _ = play.SelectedCamera.Name.Should().Be("Chase cam");
    }

    [TestMethod]
    public void PlaySection_ShowsADeletedStartingCameraAsMissing()
    {
        var scene = CreateScene();
        var camera = CameraNode(scene, fieldOfView: 60f);
        scene.SetPlay(new ScenePlayData { StartingCamera = camera.Id });
        _ = scene.RootNodes.Remove(camera);
        using var editor = new EnvironmentViewModel();

        editor.SetScene(scene);

        _ = editor.Play.SelectedCamera!.Name.Should().Be("Missing camera");
        _ = editor.Play.HasCameraError.Should().BeTrue();
        _ = editor.Play.CameraChoices[0].Name.Should().Be("Automatic");
        _ = (editor.Play.ShowsAutomaticCamera, editor.Play.ShowsNoCamera).Should().Be((false, false), "the error explains the start instead");
    }

    [TestMethod]
    public async Task PlaySection_PriorityShowsTheContextsOwnUntilOverriddenAndRowsFollowThePriority()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        scene.SetPlay(new ScenePlayData { StartingInputContexts = [new StartingInputContextData(GameplayContext), new StartingInputContextData(MenuContext)] });
        var context = CreateContext(scene);
        using var editor = new EnvironmentViewModel(fixture.Sut, () => context, inputContextSource: uri => uri == GameplayContext ? new InputContextSource(IsProjectContext: true, DeclaredPriority: 3) : InputContextSource.External);
        editor.SetScene(scene);
        var gameplay = editor.Play.Contexts[0];

        _ = (gameplay.Priority, gameplay.IsOverridden, editor.Play.Contexts[1].Priority).Should().Be((3.0, false, 0.0));
        _ = gameplay.ResetToolTip.Should().Be("Use the context's own priority (3)");

        gameplay.Priority = 8;
        await editor.Play.Pending.ConfigureAwait(false);
        _ = scene.Play.StartingInputContexts[0].Priority.Should().Be(8);
        _ = gameplay.IsOverridden.Should().BeTrue();

        gameplay.Priority = 3;
        await editor.Play.Pending.ConfigureAwait(false);
        _ = scene.Play.StartingInputContexts[0].Priority.Should().BeNull("typing the context's own priority follows it again");

        var menu = editor.Play.Contexts[1];
        menu.Priority = 5;
        await editor.Play.Pending.ConfigureAwait(false);
        _ = editor.Play.Contexts.Select(static row => row.Context).Should().Equal([MenuContext, GameplayContext], "the highest priority is evaluated first");
        _ = editor.Play.HasNoContexts.Should().BeFalse();
    }

    [TestMethod]
    public void PlaySection_AContextNoInputFileDeclaresIsMissingAndOnlyRemovable()
    {
        var scene = CreateScene();
        scene.SetPlay(new ScenePlayData { StartingInputContexts = [new StartingInputContextData(GameplayContext), new StartingInputContextData(MenuContext)] });
        using var editor = new EnvironmentViewModel(
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
        var fixture = CreateFixture();
        var scene = CreateScene();
        var first = CameraNode(scene, fieldOfView: 60f);
        var second = CameraNode(scene, fieldOfView: 60f);
        var context = CreateContext(scene);
        using var firstToggle = new StartingCameraToggle(fixture.Sut, () => context);
        using var secondToggle = new StartingCameraToggle(fixture.Sut, () => context);
        firstToggle.Bind([first]);
        secondToggle.Bind([second]);

        _ = (firstToggle.IsChecked, firstToggle.IsAutomatic, secondToggle.IsAutomatic).Should().Be((false, true, false));

        secondToggle.IsChecked = true;
        await secondToggle.Pending.ConfigureAwait(false);
        _ = scene.Play.StartingCamera.Should().Be(second.Id);
        _ = (firstToggle.IsAutomatic, secondToggle.IsChecked).Should().Be((false, true));

        firstToggle.IsChecked = true;
        await firstToggle.Pending.ConfigureAwait(false);
        _ = scene.Play.StartingCamera.Should().Be(first.Id);
        _ = (firstToggle.IsChecked, secondToggle.IsChecked).Should().Be((true, false));

        firstToggle.IsChecked = false;
        await firstToggle.Pending.ConfigureAwait(false);
        _ = scene.Play.StartingCamera.Should().BeNull();
        _ = firstToggle.IsAutomatic.Should().BeTrue();
    }
}
