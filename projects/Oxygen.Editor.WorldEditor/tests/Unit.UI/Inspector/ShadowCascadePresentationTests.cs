// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Controls;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorModels;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
internal sealed class ShadowCascadePresentationTests : VisualUserInterfaceTests
{
    [TestMethod]
    [DataRow(DirectionalCsmSplitMode.Generated, 1)]
    [DataRow(DirectionalCsmSplitMode.Generated, 2)]
    [DataRow(DirectionalCsmSplitMode.Generated, 3)]
    [DataRow(DirectionalCsmSplitMode.Generated, 4)]
    [DataRow(DirectionalCsmSplitMode.ManualDistances, 1)]
    [DataRow(DirectionalCsmSplitMode.ManualDistances, 2)]
    [DataRow(DirectionalCsmSplitMode.ManualDistances, 3)]
    [DataRow(DirectionalCsmSplitMode.ManualDistances, 4)]
    public Task CascadeFieldsOnlyEffectiveSettingsAcceptInput(DirectionalCsmSplitMode mode, int count) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        var light = fixture.Node.Components.OfType<DirectionalLightComponent>().Single();
        light.SplitMode = mode;
        light.CascadeCount = count;
        var storedDistances = light.CascadeDistances;
        using var model = (DirectionalLightViewModel)CreateModel("Light", fixture);
        var view = CreateView(model);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        AssertFields(view, mode, count);
        _ = light.CascadeDistances.Should().Be(storedDistances);
        _ = Field(view, "CascadeCount").IsEnabled.Should().BeTrue();
        _ = Field(view, "MaxShadowDistance").IsEnabled.Should().BeTrue();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });

    [TestMethod]
    public Task SplitModeChangesPreserveStoredDistancesAndUpdateAvailabilityThroughUndoRedo() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var inspector = fixture.CreateInspectorHost("Light");
        var model = inspector.PropertyEditors.OfType<DirectionalLightViewModel>().Single();
        var light = fixture.Node.Components.OfType<DirectionalLightComponent>().Single();
        var distances = light.CascadeDistances;
        var exponent = light.DistributionExponent;
        var view = CreateView(model);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var split = view.FindDescendant<ComboBox>(combo => combo.ItemsSource == model.SplitModeOptions)!;
        var first = Field(view, "CascadeDistance1");
        split.SelectedItem = DirectionalCsmSplitMode.ManualDistances;
        await model.PendingEdits.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        AssertFields(view, DirectionalCsmSplitMode.ManualDistances, 4);
        first.NumberValue = 12;
        await model.PendingEdits.ConfigureAwait(true);
        distances.X = 12;
        _ = light.CascadeDistances.Should().Be(distances);
        split.SelectedItem = DirectionalCsmSplitMode.Generated;
        await model.PendingEdits.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        AssertFields(view, DirectionalCsmSplitMode.Generated, 4);
        _ = light.CascadeDistances.Should().Be(distances);
        _ = first.NumberValue.Should().Be(12);
        _ = light.DistributionExponent.Should().Be(exponent);

        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        AssertFields(view, DirectionalCsmSplitMode.ManualDistances, 4);
        _ = light.CascadeDistances.Should().Be(distances);
        await fixture.Context.History.RedoAsync(CancellationToken.None).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        AssertFields(view, DirectionalCsmSplitMode.Generated, 4);
        _ = Field(view, "CascadeDistance1").Should().BeSameAs(first);
        _ = light.CascadeDistances.Should().Be(distances);
        var saved = (DirectionalLightData)light.Dehydrate();
        _ = saved.CascadeDistances.Should().Be(distances);
        _ = saved.DistributionExponent.Should().Be(exponent);
    });

    [TestMethod]
    public Task CascadeCountChangesMoveTheFinalBoundaryWithoutDiscardingStoredValues() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        var light = fixture.Node.Components.OfType<DirectionalLightComponent>().Single();
        light.SplitMode = DirectionalCsmSplitMode.ManualDistances;
        var distances = light.CascadeDistances;
        using var inspector = fixture.CreateInspectorHost("Light");
        var model = inspector.PropertyEditors.OfType<DirectionalLightViewModel>().Single();
        var view = CreateView(model);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        foreach (var count in new[] { 3, 2, 1, 4 })
        {
            Field(view, "CascadeCount").NumberValue = count;
            await model.PendingEdits.ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            AssertFields(view, DirectionalCsmSplitMode.ManualDistances, count);
            _ = light.CascadeDistances.Should().Be(distances);
        }

        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        AssertFields(view, DirectionalCsmSplitMode.ManualDistances, 1);
        _ = light.CascadeDistances.Should().Be(distances);
    });

    [TestMethod]
    public Task MixedSelectionsEnableOnlySettingsApplicableToEveryLight() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        var first = fixture.Node.Components.OfType<DirectionalLightComponent>().Single();
        first.SplitMode = DirectionalCsmSplitMode.ManualDistances;
        var other = new SceneNode(fixture.Scene) { Name = "Other sun" };
        var second = new DirectionalLightComponent { Name = "Other sun", SplitMode = DirectionalCsmSplitMode.ManualDistances, CascadeCount = 2 };
        _ = other.AddComponent(second);
        fixture.Scene.RootNodes.Add(other);
        using var model = (DirectionalLightViewModel)CreateModel("Light", fixture);
        model.UpdateValues([fixture.Node, other]);
        var view = CreateView(model);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        AssertFields(view, DirectionalCsmSplitMode.ManualDistances, 2);
        second.SplitMode = DirectionalCsmSplitMode.Generated;
        model.UpdateValues([fixture.Node, other]);
        await WaitForRenderAsync().ConfigureAwait(true);
        foreach (var field in view.FindDescendants().OfType<InspectorNumberField>().Where(field => field.Tag is string name
            && (name.StartsWith("CascadeDistance", StringComparison.Ordinal) || string.Equals(name, "DistributionExponent", StringComparison.Ordinal))))
        {
            _ = field.IsEnabled.Should().BeFalse();
            _ = field.ApplicabilityText.Should().BeEmpty();
        }

        first.SplitMode = DirectionalCsmSplitMode.Generated;
        second.CascadeCount = 1;
        model.UpdateValues([fixture.Node, other]);
        await WaitForRenderAsync().ConfigureAwait(true);
        AssertFields(view, DirectionalCsmSplitMode.Generated, 1);
        model.UpdateValues([fixture.Node]);
        await WaitForRenderAsync().ConfigureAwait(true);
        AssertFields(view, DirectionalCsmSplitMode.Generated, 4);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });

    private static DirectionalLightShadowsView CreateView(DirectionalLightViewModel model)
    {
        var view = new DirectionalLightShadowsView { ViewModel = model, Width = 480 };
        foreach (var expander in ((StackPanel)view.Content).Children.OfType<Expander>())
        {
            expander.IsExpanded = true;
        }

        return view;
    }

    private static InspectorNumberField Field(DirectionalLightShadowsView view, string name)
        => view.FindDescendant<InspectorNumberField>(field => Equals(field.Tag, name))!;

    private static void AssertFields(DirectionalLightShadowsView view, DirectionalCsmSplitMode mode, int count)
    {
        var distribution = Field(view, "DistributionExponent");
        _ = distribution.IsEnabled.Should().Be(mode == DirectionalCsmSplitMode.Generated && count > 1);
        _ = distribution.ApplicabilityText.Should().BeEmpty();
        for (var cascade = 1; cascade <= 4; cascade++)
        {
            var field = Field(view, $"CascadeDistance{cascade}");
            var enabled = mode == DirectionalCsmSplitMode.ManualDistances && cascade < count;
            _ = field.IsEnabled.Should().Be(enabled);
            _ = field.FindDescendant<NumberBox>()!.IsEnabled.Should().Be(enabled);
            _ = field.FindDescendants().OfType<TextBlock>().Should().NotContain(text => text.Text.StartsWith("Stored manual value;", StringComparison.Ordinal));
            _ = field.ApplicabilityText.Should().BeEmpty();
            if (!enabled)
            {
                _ = field.FindDescendant<NumberBox>()!.Focus(FocusState.Keyboard).Should().BeFalse();
            }
        }
    }
}
