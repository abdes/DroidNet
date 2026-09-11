// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks coupled sun controls through native state, history and saved source.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Sun controls maintain valid contribution and exclusive scene binding.</summary>
    /// <param name="action">The switch or scene picker action.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Sun")]
    [DataRow("Contributes")]
    [DataRow("Scene picker")]
    public Task SunControlsKeepFlagsAndSceneBindingConsistent(string action) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, SeedSunNodes);
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        VisualUserInterfaceTestsApp.MainWindow.Activate();
        var oldId = fixture.Source.RootNodes[0].Id;
        var newId = fixture.Source.RootNodes[1].Id;
        var disable = string.Equals(action, "Contributes", StringComparison.Ordinal);
        var scenePicker = string.Equals(action, "Scene picker", StringComparison.Ordinal);
        using var host = fixture.CreateInspectorHost(scenePicker ? [] : [fixture.Source.RootNodes[disable ? 0 : 1]]);
        var model = host.PropertyEditors.Single(editor => scenePicker ? editor is EnvironmentViewModel : editor is DirectionalLightViewModel);
        UserControl view = model is EnvironmentViewModel environment
            ? new EnvironmentView { ViewModel = environment }
            : new DirectionalLightView { ViewModel = (DirectionalLightViewModel)model };
        var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        if (model is EnvironmentViewModel sceneModel)
        {
            var picker = await FindSunPickerAsync((EnvironmentView)view, scroller, sceneModel).ConfigureAwait(true);
            picker.SelectedItem = sceneModel.SunOptions.Single(option => option.NodeId == newId);
            await sceneModel.PendingEdits.ConfigureAwait(true);
        }
        else
        {
            var toggle = (ToggleSwitch)await FindInspectorControlAsync(scroller, () => view.FindDescendant<ToggleSwitch>(control => Equals(control.Header, action)), action, timeout.Token).ConfigureAwait(true);
            toggle.IsOn = !disable;
            await ((DirectionalLightViewModel)model).PendingEdits.ConfigureAwait(true);
        }

        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        await AssertSunStateAsync(fixture, oldId, isSun: false, contributes: !disable, timeout.Token).ConfigureAwait(true);
        await AssertSunStateAsync(fixture, newId, isSun: !disable, contributes: !disable, timeout.Token).ConfigureAwait(true);
        _ = fixture.Source.Environment.SunNodeId.Should().Be(disable ? null : newId);
        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        await AssertSunStateAsync(fixture, oldId, isSun: true, contributes: true, timeout.Token).ConfigureAwait(true);
        await AssertSunStateAsync(fixture, newId, isSun: false, contributes: false, timeout.Token).ConfigureAwait(true);
        _ = fixture.Source.Environment.SunNodeId.Should().Be(oldId);
        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await AssertSunStateAsync(fixture, oldId, isSun: false, contributes: !disable, timeout.Token).ConfigureAwait(true);
        await AssertSunStateAsync(fixture, newId, isSun: !disable, contributes: !disable, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertSunStateAsync(fixture, oldId, isSun: false, contributes: !disable, timeout.Token).ConfigureAwait(true);
        await AssertSunStateAsync(fixture, newId, isSun: !disable, contributes: !disable, timeout.Token).ConfigureAwait(true);
        _ = fixture.Source.Environment.SunNodeId.Should().Be(disable ? null : newId);
    });

    private static void SeedSunNodes(Scene scene)
    {
        var oldSun = new SceneNode(scene) { Name = "Old sun" };
        _ = oldSun.AddComponent(new DirectionalLightComponent { Name = "Light" });
        var newSun = new SceneNode(scene) { Name = "New sun" };
        _ = newSun.AddComponent(new DirectionalLightComponent { Name = "Light", IsSunLight = false, EnvironmentContribution = false });
        scene.RootNodes.Add(oldSun);
        scene.RootNodes.Add(newSun);
        var data = scene.Dehydrate();
        scene.Hydrate(data with { Environment = scene.Environment with { SunNodeId = oldSun.Id } });
    }

    private static async Task AssertSunStateAsync(NativeSceneFixture fixture, Guid nodeId, bool isSun, bool contributes, CancellationToken cancellationToken)
    {
        var source = fixture.Source.RootNodes.Single(node => node.Id == nodeId).Components.OfType<DirectionalLightComponent>().Single();
        _ = source.IsSunLight.Should().Be(isSun);
        _ = source.EnvironmentContribution.Should().Be(contributes);
        var native = await fixture.ReadNodeAsync(nodeId, cancellationToken).ConfigureAwait(true);
        _ = native.Properties.Single(value => value.ComponentId == 3 && value.FieldId == 14).Value.Should().Be(isSun ? 1 : 0, "native sun flag for {0}", nodeId);
        _ = native.Properties.Single(value => value.ComponentId == 3 && value.FieldId == 13).Value.Should().Be(contributes ? 1 : 0, "native contribution for {0}", nodeId);
        _ = native.IsPrimarySun.Should().Be(isSun);
    }
}
