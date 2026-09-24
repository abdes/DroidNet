// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks visible sun-reference feedback after scene topology changes.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>The picker follows stored light ownership across removal, restoration and explicit clearing.</summary>
    /// <param name="removeComponent">Whether to remove the light component or its entire node.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task SunControlTracksMissingAndRestoredTargetsWithoutReauthoring(bool removeComponent) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost("Environment");
        _ = host.HasInspectorContent.Should().BeTrue();
        var model = host.PropertyEditors.OfType<EnvironmentViewModel>().Single();
        var view = new EnvironmentView { ViewModel = model };
        var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var picker = await FindSunPickerAsync(view, scroller, model).ConfigureAwait(true);
        picker.SelectedItem = model.SunOptions.Single(option => option.NodeId == fixture.Node.Id);
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Node.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot.Should().Be(Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        var light = fixture.Node.Components.OfType<DirectionalLightComponent>().Single();

        RemoveSunTarget(fixture, light, removeComponent);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = ((SunLightOption)picker.SelectedItem).NodeId.Should().BeNull();
        _ = light.AtmosphereSlot.Should().Be(Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();

        RestoreSunTarget(fixture, light, removeComponent);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = ((SunLightOption)picker.SelectedItem).NodeId.Should().Be(fixture.Node.Id);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        var clear = ((Grid)picker.Parent).Children.OfType<Button>().Single();
        ((IInvokeProvider)new ButtonAutomationPeer(clear).GetPattern(PatternInterface.Invoke)).Invoke();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = light.AtmosphereSlot.Should().Be(Oxygen.Editor.World.Serialization.AtmosphereLightSlot.None);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(2);
    });

    private static void RemoveSunTarget(Fixture fixture, DirectionalLightComponent light, bool removeComponent)
    {
        if (removeComponent)
        {
            _ = fixture.Node.RemoveComponent(light);
        }
        else
        {
            _ = fixture.Scene.RootNodes.Remove(fixture.Node);
        }
    }

    private static void RestoreSunTarget(Fixture fixture, DirectionalLightComponent light, bool removeComponent)
    {
        if (removeComponent)
        {
            _ = fixture.Node.AddComponent(light);
        }
        else
        {
            fixture.Scene.RootNodes.Add(fixture.Node);
        }
    }

    private static async Task<ComboBox> FindSunPickerAsync(EnvironmentView view, ScrollViewer scroller, EnvironmentViewModel model)
    {
        for (var step = 0; step <= 40; step++)
        {
            if (view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.SunOptions)) is { } picker)
            {
                return picker;
            }

            _ = scroller.ChangeView(horizontalOffset: null, Math.Min(scroller.ScrollableHeight, (step + 1) * scroller.ViewportHeight / 2), zoomFactor: null, disableAnimation: true);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }

        throw new InvalidOperationException("The sun picker was not realized in the environment inspector.");
    }
}
