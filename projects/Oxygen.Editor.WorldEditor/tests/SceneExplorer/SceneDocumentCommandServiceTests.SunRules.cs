// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task EnablingSunEnablesContributionAndRestoresBothLightsAndBindingOnUndo()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var oldSun = CreateDirectionalLightNode(scene, "Old sun");
        var newSun = CreateDirectionalLightNode(scene, "New sun");
        scene.RootNodes.Add(oldSun);
        scene.RootNodes.Add(newSun);
        var oldLight = oldSun.Components.OfType<DirectionalLightComponent>().Single();
        var newLight = newSun.Components.OfType<DirectionalLightComponent>().Single();
        newLight.IsSunLight = false;
        newLight.EnvironmentContribution = false;
        scene.SetEnvironment(scene.Environment with { SunNodeId = oldSun.Id });
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        using var editor = new DirectionalLightViewModel(fixture.Sut, () => context);
        editor.UpdateValues([newSun]);

        editor.IsSunLight = true;
        await editor.PendingEdits.ConfigureAwait(false);

        _ = newLight.IsSunLight.Should().BeTrue();
        _ = newLight.EnvironmentContribution.Should().BeTrue();
        _ = oldLight.IsSunLight.Should().BeFalse();
        _ = scene.Environment.SunNodeId.Should().Be(newSun.Id);
        _ = context.History.UndoStack.Should().ContainSingle();
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = newLight.IsSunLight.Should().BeFalse();
        _ = newLight.EnvironmentContribution.Should().BeFalse();
        _ = oldLight.IsSunLight.Should().BeTrue();
        _ = scene.Environment.SunNodeId.Should().Be(oldSun.Id);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = newLight.IsSunLight.Should().BeTrue();
        _ = newLight.EnvironmentContribution.Should().BeTrue();
        _ = scene.Environment.SunNodeId.Should().Be(newSun.Id);
    }

    [TestMethod]
    public async Task DisablingContributionClearsSunAndBindingInOneUndoableEdit()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var sun = CreateDirectionalLightNode(scene, "Sun");
        scene.RootNodes.Add(sun);
        var light = sun.Components.OfType<DirectionalLightComponent>().Single();
        scene.SetEnvironment(scene.Environment with { SunNodeId = sun.Id });
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        using var editor = new DirectionalLightViewModel(fixture.Sut, () => context);
        editor.UpdateValues([sun]);

        editor.EnvironmentContribution = false;
        await editor.PendingEdits.ConfigureAwait(false);

        _ = light.EnvironmentContribution.Should().BeFalse();
        _ = light.IsSunLight.Should().BeFalse();
        _ = scene.Environment.SunNodeId.Should().BeNull();
        _ = context.History.UndoStack.Should().ContainSingle();
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = light.EnvironmentContribution.Should().BeTrue();
        _ = light.IsSunLight.Should().BeTrue();
        _ = scene.Environment.SunNodeId.Should().Be(sun.Id);
    }

    [TestMethod]
    public async Task EnvironmentSunPickerEnablesContributionAndUndoRestoresTheOriginalFlag()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var sun = CreateDirectionalLightNode(scene, "Sun");
        scene.RootNodes.Add(sun);
        var light = sun.Components.OfType<DirectionalLightComponent>().Single();
        light.IsSunLight = false;
        light.EnvironmentContribution = false;
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        using var editor = new EnvironmentViewModel(fixture.Sut, () => context);
        editor.SetScene(scene);

        editor.SelectedSun = editor.SunOptions.Single(option => option.NodeId == sun.Id);
        await editor.PendingEdits.ConfigureAwait(false);

        _ = light.IsSunLight.Should().BeTrue();
        _ = light.EnvironmentContribution.Should().BeTrue();
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = light.IsSunLight.Should().BeFalse();
        _ = light.EnvironmentContribution.Should().BeFalse();
        _ = scene.Environment.SunNodeId.Should().BeNull();
    }
}
