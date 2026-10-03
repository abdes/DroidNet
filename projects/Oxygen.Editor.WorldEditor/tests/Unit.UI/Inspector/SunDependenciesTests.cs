// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
public sealed partial class SunDependenciesTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The picker follows stored light ownership across removal, restoration and explicit clearing.</summary>
    /// <param name="removeComponent">Whether to remove the light component or its entire node.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task SunControlTracksMissingAndRestoredTargetsWithoutReauthoring(bool removeComponent) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Environment");
        _ = host.HasInspectorContent.Should().BeTrue();
        var model = host.PropertyEditors.OfType<EnvironmentViewModel>().Single();
        var view = new EnvironmentView
        {
            ViewModel = model
        };
        var scroller = new ScrollViewer
        {
            Content = view,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto
        };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var picker = await FindSunPickerAsync(view, scroller, model).ConfigureAwait(true);
        picker.SelectedItem = model.SunOptions.Single(option => option.NodeId == fixture.Node.Id);
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Node.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot.Should().Be(Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        var light = fixture.Node.Components.OfType<DirectionalLightComponent>().Single();
        RemoveSunTarget(fixture, light, removeComponent);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = ((SunLightOption)picker.SelectedItem).NodeId.Should().BeNull();
        _ = light.AtmosphereSlot.Should().Be(Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        RestoreSunTarget(fixture, light, removeComponent);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = ((SunLightOption)picker.SelectedItem).NodeId.Should().Be(fixture.Node.Id);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        var clear = ((Grid)picker.Parent).Children.OfType<Button>().Single(button => ReferenceEquals(button.Command, model.ClearSunCommand));
        ((IInvokeProvider)new ButtonAutomationPeer(clear).GetPattern(PatternInterface.Invoke)).Invoke();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = light.AtmosphereSlot.Should().Be(Oxygen.Editor.World.Serialization.AtmosphereLightSlot.None);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(2);
    });

    /// <summary>Inspect waits for Explorer selection before applying its component filter without authoring edits.</summary>
    /// <returns>The navigation regression task.</returns>
    [TestMethod]
    public Task InspectAtmosphereSourceWaitsForSelectionAndPreservesDirectionalFilter() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Environment");
        var model = host.PropertyEditors.OfType<EnvironmentViewModel>().Single();
        var source = model.SunOptions.Single(option => option.NodeId == fixture.Node.Id);
        fixture.Messenger.Register<InspectSceneNodeMessage>(this, (_, message) => message.Reply(RevealAsync()));

        async Task<bool> RevealAsync()
        {
            await Task.Yield();
            _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([]));
            _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([fixture.Node]));
            return true;
        }

        await model.InspectAtmosphereSourceAsync(source).ConfigureAwait(true);
        _ = host.SelectedNode.Should().BeSameAs(fixture.Node);
        _ = host.SelectedComponentType.Should().Be(typeof(DirectionalLightComponent));
        _ = host.PropertyEditors.Should().ContainSingle().Which.Should().BeOfType<DirectionalLightViewModel>();
        await model.InspectAtmosphereSourceAsync(source).ConfigureAwait(true);
        _ = host.SelectedComponentType.Should().Be(typeof(DirectionalLightComponent));
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });

    /// <summary>Source reset restores bindings and source values in one undoable command without changing illumination.</summary>
    /// <returns>The authoring regression task.</returns>
    [TestMethod]
    public Task ResetAtmosphereSourcesRestoresInitialSettingsInOneUndoStep() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        var light = fixture.Node.Components.OfType<DirectionalLightComponent>().Single();
        light.AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary;
        var originalAngularSize = light.AngularSizeRadians;
        using var host = fixture.CreateInspectorHost("Environment");
        var model = host.PropertyEditors.OfType<EnvironmentViewModel>().Single();
        model.PrimaryAtmosphereSource.AngularDiameterDegrees = 1;
        await model.PrimaryAtmosphereSource.PendingEdits.ConfigureAwait(true);
        model.ClearSunCommand.Execute(null);
        await model.PendingEdits.ConfigureAwait(true);
        var editedAngularSize = light.AngularSizeRadians;
        var historyCount = fixture.Context.History.UndoStack.Count;
        var intensity = light.IntensityLux;
        await model.ResetAtmosphereSourcesAsync().ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = model.SunReferenceDiagnostic.Message.Should().BeEmpty();
        _ = light.AtmosphereSlot.Should().Be(Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary);
        _ = light.AngularSizeRadians.Should().Be(originalAngularSize);
        _ = light.IntensityLux.Should().Be(intensity);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(historyCount + 1);
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = light.AtmosphereSlot.Should().Be(Oxygen.Editor.World.Serialization.AtmosphereLightSlot.None);
        _ = light.AngularSizeRadians.Should().Be(editedAngularSize);
    });

    private static void RemoveSunTarget(SceneAuthoringFixture fixture, DirectionalLightComponent light, bool removeComponent)
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

    private static void RestoreSunTarget(SceneAuthoringFixture fixture, DirectionalLightComponent light, bool removeComponent)
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
}
