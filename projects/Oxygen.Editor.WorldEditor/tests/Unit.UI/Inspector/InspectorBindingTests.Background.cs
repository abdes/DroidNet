// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorModels;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using Expander = Microsoft.UI.Xaml.Controls.Expander;
using PropertiesExpander = Oxygen.Editor.Controls.PropertiesExpander;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

public sealed partial class InspectorBindingTests
{
    [TestMethod]
    public Task ExposureCurvePreviewUsesViewOwnedPointsAndTracksUndo() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(
            LoadTestContentAsync,
            420,
            650,
            host: ScenarioHost.None,
            prepareModel: static model =>
            {
                model.Exposure.ExposureMode = ExposureMode.Auto;
                return model.PendingEdits;
            }).ConfigureAwait(true);
        await scenario.SearchAsync("Exposure-compensation").ConfigureAwait(true);
        var section = scenario.Section("ExposureSection");
        var disclosure = section.Items.OfType<Expander>().Single(item => string.Equals(item.Header as string, "Exposure shaping", StringComparison.Ordinal));
        disclosure.IsExpanded = true;
        _ = section.BringItemIntoView(disclosure);
        await WaitForRenderAsync().ConfigureAwait(true);
        var preview = scenario.View.FindDescendants().OfType<Microsoft.UI.Xaml.Shapes.Polyline>().Single();
        var original = preview.Points.Select(point => (point.X, point.Y)).ToArray();

        scenario.Model.Exposure.AutoExposureCompensationCurve =
        [
            new ExposureCompensationKeyData(-2, -1),
            new ExposureCompensationKeyData(0, 1),
            new ExposureCompensationKeyData(2, 0),
        ];
        await scenario.Model.PendingEdits.ConfigureAwait(true);

        _ = preview.Points.Select(point => (point.X, point.Y)).Should().Equal((0d, 32d), (80d, 4d), (160d, 18d));
        await scenario.Fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = preview.Points.Select(point => (point.X, point.Y)).Should().Equal(original);
    });

    [TestMethod]
    public Task BackgroundResetIsUndoableAndPreservesOtherSceneSettings() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, width: null, height: null, host: ScenarioHost.None).ConfigureAwait(true);
        await scenario.SearchAsync("background").ConfigureAwait(true);

        scenario.Model.Background.SetBackgroundColor(InspectorRgbPresentation.ToLinearRgb(Windows.UI.Color.FromArgb(255, 128, 64, 32)));
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        var original = scenario.Fixture.Scene.Environment;
        var count = scenario.Fixture.Context.History.UndoStack.Count;
        var reset = scenario.Element<Button>("ResetBackgroundButton");
        ((IInvokeProvider)new ButtonAutomationPeer(reset).GetPattern(PatternInterface.Invoke)).Invoke();
        await scenario.Model.PendingEdits.ConfigureAwait(true);

        _ = scenario.Fixture.Scene.Environment.BackgroundColor.Should().Be(Vector3.Zero);
        _ = scenario.Fixture.Scene.Environment.Should().Be(original with { BackgroundColor = Vector3.Zero });
        _ = scenario.Fixture.Context.Metadata.IsDirty.Should().BeTrue();
        _ = scenario.Fixture.Context.History.UndoStack.Should().HaveCount(count + 1);
        _ = scenario.Model.Background.BackgroundR.Should().Be(0);
        _ = scenario.Model.Background.BackgroundG.Should().Be(0);
        _ = scenario.Model.Background.BackgroundB.Should().Be(0);

        await scenario.Fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = scenario.Fixture.Scene.Environment.Should().Be(original);
        _ = new Vector3(scenario.Model.Background.BackgroundR, scenario.Model.Background.BackgroundG, scenario.Model.Background.BackgroundB).Should().Be(original.BackgroundColor);
        await scenario.Fixture.Context.History.RedoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = scenario.Fixture.Scene.Environment.BackgroundColor.Should().Be(Vector3.Zero);
    });

    [TestMethod]
    public Task BackgroundPickerAndLinearChannelsKeepDisplaySwatchInSync() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, width: null, height: null, host: ScenarioHost.None).ConfigureAwait(true);
        await scenario.SearchAsync("background").ConfigureAwait(true);
        var field = scenario.Element<Oxygen.Editor.World.Inspector.Controls.InspectorRgbField>("BackgroundColorCard");
        var swatch = (Button)((Oxygen.Editor.Controls.PropertyCard)field.Content).LeadingContent!;
        var displayColor = Windows.UI.Color.FromArgb(255, 128, 64, 32);
        await PickDisplayColorAsync(swatch, displayColor).ConfigureAwait(true);
        await scenario.Model.PendingEdits.ConfigureAwait(true);

        _ = scenario.Model.Background.BackgroundR.Should().BeApproximately(0.21586f, 0.00001f);
        _ = scenario.Model.Background.BackgroundG.Should().BeApproximately(0.05127f, 0.00001f);
        _ = scenario.Model.Background.BackgroundB.Should().BeApproximately(0.01444f, 0.00001f);
        _ = scenario.Model.Background.BackgroundColor.Should().Be(scenario.Fixture.Scene.Environment.BackgroundColor);
        _ = ((Microsoft.UI.Xaml.Media.SolidColorBrush)((Border)swatch.Content).Background).Color.Should().Be(displayColor);
        scenario.Model.Background.BackgroundR = 1;
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        var display = ((Microsoft.UI.Xaml.Media.SolidColorBrush)((Border)swatch.Content).Background).Color;
        _ = display.R.Should().Be(255);
        _ = display.G.Should().Be(64);
        _ = display.B.Should().Be(32);
        _ = scenario.Fixture.Scene.Environment.BackgroundColor.Should().Be(new Vector3(1, scenario.Model.Background.BackgroundG, scenario.Model.Background.BackgroundB));
    });
}
