// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldCases;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldControls;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Inspector;

[TestClass]
public sealed partial class EnvironmentSynchronizationTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A field edit, history replay and saved reopen preserve authored and native values.</summary>
    /// <param name="fieldName">The field's inspector binding name.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("AtmosphereEnabled")]
    [DataRow("SunDiskEnabled")]
    [DataRow("PlanetRadiusKm")]
    [DataRow("AtmosphereHeightKm")]
    [DataRow("GroundAlbedoR")]
    [DataRow("GroundAlbedoG")]
    [DataRow("GroundAlbedoB")]
    [DataRow("RayleighScaleHeightKm")]
    [DataRow("MieScaleHeightKm")]
    [DataRow("MieAnisotropy")]
    [DataRow("SkyLuminanceR")]
    [DataRow("SkyLuminanceG")]
    [DataRow("SkyLuminanceB")]
    [DataRow("AerialPerspectiveDistanceScale")]
    [DataRow("AerialScatteringStrength")]
    [DataRow("AerialPerspectiveStartDepthMeters")]
    [DataRow("HeightFogContribution")]
    [DataRow("ExposureMode")]
    [DataRow("ExposureEnabled")]
    [DataRow("ExposureKey")]
    [DataRow("ManualExposureEv")]
    [DataRow("ExposureCompensation")]
    [DataRow("ToneMapping")]
    [DataRow("AutoExposureMeteringMode")]
    [DataRow("AutoExposureMinEv")]
    [DataRow("AutoExposureMaxEv")]
    [DataRow("AutoExposureSpeedUp")]
    [DataRow("AutoExposureSpeedDown")]
    [DataRow("AutoExposureLowPercentile")]
    [DataRow("AutoExposureHighPercentile")]
    [DataRow("AutoExposureMinLogLuminance")]
    [DataRow("AutoExposureLogLuminanceRange")]
    [DataRow("AutoExposureTargetLuminance")]
    [DataRow("AutoExposureSpotMeterRadius")]
    [DataRow("BloomIntensity")]
    [DataRow("BloomThreshold")]
    [DataRow("Saturation")]
    [DataRow("Contrast")]
    [DataRow("VignetteIntensity")]
    [DataRow("DisplayGamma")]
    public Task EnvironmentFieldControlHistoryAndReopenReachNativeState(string fieldName) => EnqueueAsync(async () =>
    {
        var field = NativeEnvironmentFields.Single(value => string.Equals(value.Field, fieldName, StringComparison.Ordinal));
        var fixture = new NativeSceneFixture(field.Automatic);
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        VisualUserInterfaceTestsApp.MainWindow.Activate();
        var view = new EnvironmentView
        {
            ViewModel = fixture.Model,
        };
        var scroller = new ScrollViewer
        {
            Content = view,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var control = await FindEnvironmentFieldControlAsync(view, scroller, fixture.Model, field, timeout.Token).ConfigureAwait(true);
        await fixture.Model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty("realizing a control must not author values");
        var before = NativeEnvironmentFields.ToDictionary(value => value.Field, value => value.ReadSource(fixture.Source.Environment), StringComparer.Ordinal);
        await AssertEnvironmentFieldValuesAsync(fixture, before, timeout.Token).ConfigureAwait(true);
        await SetEnvironmentControlValueAsync(control, field.ControlValue).ConfigureAwait(true);
        await fixture.Model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        var expected = new Dictionary<string, object>(before, StringComparer.Ordinal)
        {
            [field.Field] = field.ExpectedValue,
        };
        await AssertEnvironmentFieldValuesAsync(fixture, expected, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        await AssertEnvironmentFieldValuesAsync(fixture, before, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await AssertEnvironmentFieldValuesAsync(fixture, expected, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertEnvironmentFieldValuesAsync(fixture, expected, timeout.Token).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });
}
