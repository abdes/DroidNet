// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks each environment control against native state and reopened source.</summary>
public sealed partial class InspectorControlTests
{
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
        var view = new EnvironmentView { ViewModel = fixture.Model };
        var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var control = await FindEnvironmentFieldControlAsync(view, scroller, fixture.Model, field, timeout.Token).ConfigureAwait(true);
        await fixture.Model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty("realizing a control must not author values");
        var before = NativeEnvironmentFields.ToDictionary(value => value.Field, value => value.ReadSource(fixture.Source.Environment), StringComparer.Ordinal);
        await AssertEnvironmentFieldValuesAsync(fixture, before, timeout.Token).ConfigureAwait(true);

        await SetEnvironmentControlValueAsync(control, field.ControlValue).ConfigureAwait(true);
        await fixture.Model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        var expected = new Dictionary<string, object>(before, StringComparer.Ordinal) { [field.Field] = field.ExpectedValue };
        await AssertEnvironmentFieldValuesAsync(fixture, expected, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        await AssertEnvironmentFieldValuesAsync(fixture, before, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await AssertEnvironmentFieldValuesAsync(fixture, expected, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertEnvironmentFieldValuesAsync(fixture, expected, timeout.Token).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    private static async Task AssertEnvironmentFieldValuesAsync(NativeSceneFixture fixture, Dictionary<string, object> expected, CancellationToken cancellationToken)
    {
        var native = await fixture.ReadNativeAsync(cancellationToken).ConfigureAwait(true);
        foreach (var field in NativeEnvironmentFields)
        {
            _ = field.ReadSource(fixture.Source.Environment).Should().Be(expected[field.Field], "source field {0}", field.Field);
            _ = field.ReadNative(native).Should().Be(expected[field.Field], "native field {0}", field.Field);
        }

        foreach (var property in typeof(EnvironmentViewModel).GetProperties().Where(property => property.PropertyType == typeof(InspectorFieldDiagnostic)))
        {
            _ = ((InspectorFieldDiagnostic)property.GetValue(fixture.Model)!).Message.Should().BeEmpty("valid field workflow must have no {0} error", property.Name);
        }
    }

    private static async Task SetEnvironmentControlValueAsync(FrameworkElement control, object value)
    {
        switch (control)
        {
            case NumberBox number:
                await EnterTextAsync(number, ((float)value).ToString(CultureInfo.CurrentCulture)).ConfigureAwait(true);
                number.CompletePendingTextEdit();
                break;
            case ToggleSwitch toggle:
                toggle.IsOn = (bool)value;
                break;
            case CheckBox check:
                check.IsChecked = (bool)value;
                break;
            case ComboBox combo:
                combo.SelectedItem = value;
                break;
            default:
                throw new InvalidOperationException($"Unsupported environment control {control.GetType().Name}.");
        }
    }

    private static async Task<FrameworkElement> FindEnvironmentFieldControlAsync(EnvironmentView view, ScrollViewer scroller, EnvironmentViewModel model, EnvironmentFieldCase field, CancellationToken cancellationToken)
    {
        foreach (var section in view.FindDescendants().OfType<Oxygen.Editor.Controls.PropertiesExpander>())
        {
            section.IsExpanded = true;
        }

        return await FindInspectorControlAsync(scroller, () => FindEnvironmentControl(view, model, field), field.Field, cancellationToken).ConfigureAwait(true);
    }

    private static ToggleSwitch? FindEnvironmentToggle(EnvironmentView view, string section)
        => view.FindDescendant<ToggleSwitch>(element => Equals(element.FindAscendant<Oxygen.Editor.Controls.PropertiesExpander>()?.Header, section));

    private static FrameworkElement? FindEnvironmentControl(EnvironmentView view, EnvironmentViewModel model, EnvironmentFieldCase field)
        => field.Field switch
        {
            "AtmosphereEnabled" => FindEnvironmentToggle(view, "Sky Atmosphere"),
            "ExposureEnabled" => FindEnvironmentToggle(view, "Exposure"),
            "SunDiskEnabled" => view.FindDescendant<CheckBox>(),
            "ExposureMode" => view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.ExposureModes)),
            "ToneMapping" => view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.ToneMappingModes)),
            "AutoExposureMeteringMode" => view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.MeteringModes)),
            _ when field.VectorTag is { } tag => view.FindDescendant<VectorBox>(element => Equals(element.Tag, tag))?.FindDescendant<NumberBox>(element => string.Equals(element.Name, $"PartNumberBox{field.VectorAxis}", StringComparison.Ordinal)),
            _ => view.FindDescendant<NumberBox>(element => Equals(element.Tag, field.Field)),
        };
}
