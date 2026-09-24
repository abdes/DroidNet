// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Tests;

public sealed partial class InspectorControlTests
{
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task OccupiedPrimaryRejectsInspectorAndEnvironmentPickerEdits(bool scenePicker) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, SeedSunNodes);
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        VisualUserInterfaceTestsApp.MainWindow.Activate();
        var owner = fixture.Source.RootNodes[0];
        var candidate = fixture.Source.RootNodes[1];
        using var host = fixture.CreateInspectorHost(scenePicker ? [] : [candidate]);
        var model = host.PropertyEditors.Single(editor => scenePicker ? editor is EnvironmentViewModel : editor is DirectionalLightViewModel);
        UserControl view = model is EnvironmentViewModel environment
            ? new EnvironmentView { ViewModel = environment }
            : new DirectionalLightView { ViewModel = (DirectionalLightViewModel)model };
        var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        if (model is EnvironmentViewModel sceneModel)
        {
            var picker = await FindSunPickerAsync((EnvironmentView)view, scroller, sceneModel).ConfigureAwait(true);
            picker.SelectedItem = sceneModel.SunOptions.Single(option => option.NodeId == candidate.Id);
            await sceneModel.PendingEdits.ConfigureAwait(true);
        }
        else
        {
            var lightModel = (DirectionalLightViewModel)model;
            var picker = (ComboBox)await FindInspectorControlAsync(scroller,
                () => view.FindDescendant<ComboBox>(control => ReferenceEquals(control.ItemsSource, lightModel.AtmosphereSlotOptions)),
                "Source assignment", timeout.Token).ConfigureAwait(true);
            picker.SelectedItem = AtmosphereLightSlot.Primary;
            await lightModel.PendingEdits.ConfigureAwait(true);
        }
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        foreach (var node in new[] { owner, candidate })
        {
            var expected = node == owner ? AtmosphereLightSlot.Primary : AtmosphereLightSlot.None;
            _ = node.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot.Should().Be(expected);
            var native = await fixture.ReadNodeAsync(node.Id, timeout.Token).ConfigureAwait(true);
            _ = native.Properties.Single(value => value.ComponentId == 3 && value.FieldId == 25).Value.Should().Be((int)expected);
        }
    });

    private static void SeedSunNodes(Scene scene)
    {
        var owner = new SceneNode(scene) { Name = "Primary owner" };
        _ = owner.AddComponent(new DirectionalLightComponent { Name = "Light", AtmosphereSlot = AtmosphereLightSlot.Primary });
        var candidate = new SceneNode(scene) { Name = "Unassigned light" };
        _ = candidate.AddComponent(new DirectionalLightComponent { Name = "Light" });
        scene.RootNodes.Add(owner);
        scene.RootNodes.Add(candidate);
    }
}
