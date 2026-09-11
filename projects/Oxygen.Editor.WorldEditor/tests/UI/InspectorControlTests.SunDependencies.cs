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
    /// <summary>The selected sun becomes stale on removal, recovers on restoration, and clears only on explicit user action.</summary>
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
        _ = fixture.Scene.Environment.SunNodeId.Should().Be(fixture.Node.Id);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        var warning = ((Grid)picker.Parent).Children.OfType<InfoBar>().Single();
        var light = fixture.Node.Components.OfType<DirectionalLightComponent>().Single();

        RemoveSunTarget(fixture, light, removeComponent);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = warning.IsOpen.Should().BeTrue();
        _ = warning.Message.Should().NotBeEmpty();
        _ = ((SunLightOption)picker.SelectedItem).NodeId.Should().BeNull();
        _ = fixture.Scene.Environment.SunNodeId.Should().Be(fixture.Node.Id);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();

        RestoreSunTarget(fixture, light, removeComponent);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = warning.IsOpen.Should().BeFalse();
        _ = ((SunLightOption)picker.SelectedItem).NodeId.Should().Be(fixture.Node.Id);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        var clear = ((Grid)picker.Parent).Children.OfType<Button>().Single();
        ((IInvokeProvider)new ButtonAutomationPeer(clear).GetPattern(PatternInterface.Invoke)).Invoke();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Scene.Environment.SunNodeId.Should().BeNull();
        _ = warning.IsOpen.Should().BeFalse();
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
