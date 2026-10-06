// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Environment;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorModels;
using Expander = Microsoft.UI.Xaml.Controls.Expander;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "partial class with public part")]
public sealed partial class InspectorBindingTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The transform inspector realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task TransformInspectorXamlRealizesItsNumericControls() => this.AssertInspectorRealizesAsync(
        static fixture => CreateModel("Transform", fixture),
        static model => new TransformView { ViewModel = (TransformViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: null);

    /// <summary>The camera inspector realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task CameraInspectorXamlRealizesItsNumericControls() => this.AssertInspectorRealizesAsync(
        static fixture => CreateModel("Camera", fixture),
        static model => new PerspectiveCameraView { ViewModel = (PerspectiveCameraViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: null);

    /// <summary>The light inspector realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task LightInspectorXamlRealizesItsNumericControls() => this.AssertInspectorRealizesAsync(
        static fixture => CreateModel("Light", fixture),
        static model => new DirectionalLightView { ViewModel = (DirectionalLightViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: null);

    /// <summary>The atmosphere-lights inspector realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task AtmosphereLightsXamlRealizesItsNumericControls() => this.AssertInspectorRealizesAsync(
        static fixture => CreateModel("Environment", fixture),
        static model => new AtmosphereLightsSectionView { ViewModel = ((EnvironmentViewModel)model).AtmosphereLights },
        static fixture => fixture.Node.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary,
        async view =>
        {
            var section = view.FindDescendant<Oxygen.Editor.Controls.PropertiesExpander>()!;
            var sources = section.Items.OfType<Oxygen.Editor.Controls.PropertyCard>().Single();
            _ = section.BringItemIntoView(sources).Should().BeTrue();
            await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            var primary = sources.FindDescendants().OfType<Expander>().First();
            primary.IsExpanded = true;
            await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            _ = view.FindDescendants().OfType<NumberBox>().Should().Contain(number => Equals(number.Tag, "SunAzimuth"));
        });

    /// <summary>The material editor realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task MaterialEditorXamlRealizesItsNumericControls() => this.AssertInspectorRealizesAsync(
        static fixture => CreateModel("Material", fixture),
        static model => new MaterialEditorView { ViewModel = (MaterialEditorViewModel)model },
        prepareFixture: null,
        assertRealizedAsync: async view =>
        {
            var scroller = view.FindDescendant<ScrollViewer>()!;
            _ = await FindInspectorControlAsync(scroller, () => view.FindDescendant<NumberBox>(number => Equals(number.Tag, "RoughnessFactor")), "Material.RoughnessFactor", this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = view.KeyboardAccelerators.Should().OnlyContain(accelerator => ReferenceEquals(accelerator.ScopeOwner, view));
        });

    /// <summary>Sky luminance keeps its single-line header and full-width RGB multiplier vector at both dock widths.</summary>
    /// <param name="width">The available inspector width.</param>
    /// <returns>The native layout regression task.</returns>
    [TestMethod]
    [DataRow(350d)]
    [DataRow(260d)]
    public Task SkyLuminanceCardUsesSingleLineHeaderAndFullWidthVector(double width) => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, width).ConfigureAwait(true);
        await scenario.SearchAsync("sky_luminance_factor_rgb").ConfigureAwait(true);
        var sky = scenario.Section("SkyAtmosphereSection");
        var card = SceneCards(sky).Single(property => string.Equals(property.PropertyName, "Sky Luminance", StringComparison.Ordinal));
        _ = sky.BringItemIntoView(card);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var label = card.FindDescendant<TextBlock>(text => string.Equals(text.Text, card.PropertyName, StringComparison.Ordinal))!;
        var unit = card.FindDescendant<TextBlock>(text => string.Equals(text.Text, "RGB ×", StringComparison.Ordinal))!;
        var channels = card.FindDescendant<VectorBox>()!;
        _ = label.TextWrapping.Should().Be(TextWrapping.NoWrap);
        _ = label.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
        _ = channels.ActualWidth.Should().BeApproximately(card.ActualWidth, 1);
        _ = channels.TransformToVisual(card).TransformPoint(default).Y.Should().BeGreaterThanOrEqualTo(
            label.TransformToVisual(card).TransformPoint(default).Y + label.ActualHeight);
        _ = unit.TransformToVisual(card).TransformPoint(default).X.Should().BeGreaterThan(
            label.TransformToVisual(card).TransformPoint(default).X);
        _ = channels.ComponentLabels.Values.Should().Equal("R", "G", "B");
        var inputs = channels.FindDescendants().OfType<NumberBox>().ToArray();
        _ = inputs.Should().HaveCount(3);
        _ = inputs.Should().OnlyContain(input => input.ActualWidth > 50);
        _ = card.FindDescendants().OfType<ColorPicker>().Should().BeEmpty();
        scenario.AssertCleanHistory();
        await InspectorCapture.SaveIfRequestedAsync(card, $"sky-luminance-{width}").ConfigureAwait(true);
    });

    /// <summary>Sky luminance channels keep the compact contract and their geometry while an edit session is open.</summary>
    /// <param name="width">The available inspector width.</param>
    /// <returns>The native edit-session regression task.</returns>
    [TestMethod]
    [DataRow(350d)]
    public Task SkyLuminanceChannelsKeepCompactContractAndGeometryDuringEdit(double width) => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, width).ConfigureAwait(true);
        await scenario.SearchAsync("sky_luminance_factor_rgb").ConfigureAwait(true);
        var sky = scenario.Section("SkyAtmosphereSection");
        var card = SceneCards(sky).Single(property => string.Equals(property.PropertyName, "Sky Luminance", StringComparison.Ordinal));
        _ = sky.BringItemIntoView(card);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var channels = card.FindDescendant<VectorBox>()!;
        var inputs = channels.FindDescendants().OfType<NumberBox>().ToArray();
        foreach (var (part, letter, label, color) in RgbChannels)
        {
            var input = inputs.Single(number => string.Equals(number.Name, part, StringComparison.Ordinal));
            _ = input.IsCompact.Should().BeTrue();
            AssertRgbChannelBasics(input, label, color);
            await AssertEditSessionPreservesGeometryAsync(
                input,
                channels,
                whileEditing: async editor =>
                {
                    _ = Microsoft.UI.Xaml.Input.FocusManager.GetFocusedElement(editor.XamlRoot).Should().BeSameAs(editor);

                    // debug capture hold
                    if (string.Equals(letter, "R", StringComparison.Ordinal))
                    {
                        await InspectorCapture.HoldIfRequestedAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
                    }
                }).ConfigureAwait(true);
        }
    });

    /// <summary>Background color uses a full-width two-row editor with visible colored channels.</summary>
    /// <param name="width">The available inspector width.</param>
    /// <returns>The native color layout regression task.</returns>
    [TestMethod]
    [DataRow(350d)]
    [DataRow(260d)]
    public Task BackgroundColorCardUsesTwoRowLayoutWithSwatch(double width) => EnqueueAsync(async () =>
    {
        // Theme matrix dropped — no assertion is theme-dependent; Dark is the editor default.
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, width, theme: ElementTheme.Dark).ConfigureAwait(true);
        await scenario.SearchAsync("background").ConfigureAwait(true);
        var section = scenario.Section("BackgroundSection");
        _ = section.Description.Should().Be("Fallback color when atmosphere rendering is disabled.");
        _ = scenario.Element<Button>("ResetBackgroundButton").IsEnabled.Should().BeTrue();
        var card = SceneCards(section).Single(property => string.Equals(property.PropertyName, "Color", StringComparison.Ordinal));
        _ = section.BringItemIntoView(scenario.Named("BackgroundColorCard"));
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var label = card.FindDescendant<TextBlock>(text => string.Equals(text.Text, "Color", StringComparison.Ordinal))!;
        var unit = card.FindDescendant<TextBlock>(text => string.Equals(text.Text, "Linear RGB", StringComparison.Ordinal))!;
        var swatch = (Button)card.LeadingContent!;
        var channels = card.FindDescendant<VectorBox>()!;
        var channelsPosition = channels.TransformToVisual(card).TransformPoint(default);
        var swatchPosition = swatch.TransformToVisual(card).TransformPoint(default);
        _ = label.TransformToVisual(card).TransformPoint(default).X.Should().BeApproximately(0, 1);
        _ = (unit.TransformToVisual(card).TransformPoint(default).X + unit.ActualWidth).Should().BeApproximately(card.ActualWidth, 1);
        _ = channelsPosition.Y.Should().BeGreaterThanOrEqualTo(label.TransformToVisual(card).TransformPoint(default).Y + label.ActualHeight);
        _ = swatchPosition.X.Should().BeApproximately(0, 1);
        _ = (channelsPosition.X - swatchPosition.X - swatch.ActualWidth).Should().BeApproximately(4, 1);
        _ = channelsPosition.Y.Should().BeApproximately(swatchPosition.Y, 1);
        _ = (channelsPosition.X + channels.ActualWidth).Should().BeApproximately(card.ActualWidth, 1);
        _ = swatch.ActualHeight.Should().BeApproximately(32, 1);
        var inputs = channels.FindDescendants().OfType<NumberBox>().ToArray();
        _ = inputs.Should().HaveCount(3);
        foreach (var row in RgbChannels)
        {
            var input = inputs.Single(number => string.Equals(number.Name, row.part, StringComparison.Ordinal));
            var value = input.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PartValueTextBlock", StringComparison.Ordinal))!;
            _ = input.ActualWidth.Should().BeGreaterThan(50);
            _ = input.ActualHeight.Should().BeApproximately(32, 1);
            AssertRgbChannelBasics(input, row.label, row.color);
            _ = value.Text.Should().Be("0");
            _ = value.Visibility.Should().Be(Visibility.Visible);
            _ = value.Opacity.Should().Be(1);
            _ = value.ActualWidth.Should().BeGreaterThan(value.Padding.Left + value.Padding.Right);
            _ = value.ActualWidth.Should().BeGreaterThanOrEqualTo(value.DesiredSize.Width - 1);
            _ = (value.TransformToVisual(input).TransformPoint(default).X + value.ActualWidth).Should().BeLessThanOrEqualTo(input.ActualWidth + 1);
        }

        scenario.AssertCleanHistory();
        await InspectorCapture.SaveIfRequestedAsync(section, $"background-{width}").ConfigureAwait(true);
    });

    /// <summary>Background color channel edit sessions preserve their geometry and seeded display value.</summary>
    /// <param name="width">The available inspector width.</param>
    /// <returns>The native color edit-session regression task.</returns>
    [TestMethod]
    [DataRow(350d)]
    public Task BackgroundColorChannelEditSessionsKeepGeometry(double width) => EnqueueAsync(async () =>
    {
        // Theme matrix dropped — no assertion is theme-dependent; Dark is the editor default.
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, width, theme: ElementTheme.Dark).ConfigureAwait(true);
        await scenario.SearchAsync("background").ConfigureAwait(true);
        var section = scenario.Section("BackgroundSection");
        var card = SceneCards(section).Single(property => string.Equals(property.PropertyName, "Color", StringComparison.Ordinal));
        _ = section.BringItemIntoView(scenario.Named("BackgroundColorCard"));
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var channels = card.FindDescendant<VectorBox>()!;
        var inputs = channels.FindDescendants().OfType<NumberBox>().ToArray();
        foreach (var (part, letter, label, color) in RgbChannels)
        {
            var input = inputs.Single(number => string.Equals(number.Name, part, StringComparison.Ordinal));
            await AssertEditSessionPreservesGeometryAsync(
                input,
                channels,
                whileEditing: editor =>
                {
                    _ = editor.Text.Should().Be("0");
                    return Task.CompletedTask;
                }).ConfigureAwait(true);
        }

        await InspectorCapture.HoldIfRequestedAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        scenario.AssertCleanHistory();
    });

    /// <summary>Compact scalar fields keep their 30px height and the four-pixel qualifier gap through an edit session.</summary>
    /// <returns>The compact-metric regression task.</returns>
    [TestMethod]
    public Task AtmosphereScalarUnitsUseCompactFieldsAndFourPixelGaps() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, 350).ConfigureAwait(true);
        await scenario.SearchAsync("planet_radius").ConfigureAwait(true);
        var number = scenario.View.FindDescendant<NumberBox>(input => Equals(input.Tag, "PlanetRadiusKm"))!;
        var suffix = number.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PartValueQualifier", StringComparison.Ordinal))!;
        var field = number.FindDescendant<Border>(border => string.Equals(border.Name, "PartBackgroundBorder", StringComparison.Ordinal))!;
        _ = number.Label.Should().NotBeNullOrEmpty();
        _ = field.ActualHeight.Should().BeApproximately(30, 1);
        _ = number.ActualWidth.Should().BeGreaterThan(100);
        _ = number.CornerRadius.Should().Be(new CornerRadius(4));
        var gap = suffix.TransformToVisual(scenario.View).TransformPoint(default).X
            - field.TransformToVisual(scenario.View).TransformPoint(default).X - field.ActualWidth - field.Margin.Right;
        _ = gap.Should().BeApproximately(4, 1);
        var beforeHeight = number.ActualHeight;
        var beforeWidth = number.ActualWidth;
        number.StartEdit();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = number.ActualHeight.Should().BeApproximately(beforeHeight, 1);
        _ = number.ActualWidth.Should().BeApproximately(beforeWidth, 1);
        number.CancelEdit();
    });

    /// <summary>The atmosphere source disclosures start collapsed and occupy only their header height.</summary>
    /// <returns>The source-disclosure layout task.</returns>
    [TestMethod]
    public Task AtmosphereSourceDisclosuresStartCollapsedWithHeaderOnlyHeight() => EnqueueAsync(async () =>
    {
        var (fixture, model, view) = await LoadAtmosphereLightsSectionAsync(LoadTestContentAsync).ConfigureAwait(true);
        var atmosphereSection = (Oxygen.Editor.Controls.PropertiesExpander)view.FindName("AtmosphereLightsSection");
        var sourceCard = atmosphereSection.Items.OfType<Oxygen.Editor.Controls.PropertyCard>().Single();
        _ = atmosphereSection.BringItemIntoView(sourceCard).Should().BeTrue();
        await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var sourceDisclosures = sourceCard.FindDescendants().OfType<Expander>().ToArray();
        _ = sourceDisclosures.Should().HaveCount(2);
        var primary = sourceDisclosures[0];
        var secondary = sourceDisclosures[1];
        _ = primary.IsExpanded.Should().BeFalse();
        _ = secondary.IsExpanded.Should().BeFalse();
        var collapsedHeader = primary.FindDescendant<ToggleButton>(toggle => string.Equals(toggle.Name, "ExpanderHeader", StringComparison.Ordinal))!;
        _ = primary.ActualHeight.Should().BeApproximately(collapsedHeader.ActualHeight, 1);
        var separator = ((StackPanel)sourceCard.Content).Children.OfType<Grid>().Single().Children.OfType<Border>().Single();
        var separatorGap = separator.TransformToVisual(view).TransformPoint(default).Y
            - collapsedHeader.TransformToVisual(view).TransformPoint(default).Y - collapsedHeader.ActualHeight;
        _ = separatorGap.Should().BeApproximately(8, 1);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>Atmosphere source fields use left labels, the design ratio gaps and the four-pixel row pitch.</summary>
    /// <returns>The source-field metrics task.</returns>
    [TestMethod]
    public Task AtmosphereSourceFieldsUseLeftLabelsAndDesignRatioGaps() => EnqueueAsync(async () =>
    {
        // design-spec label column ratio and field padding
        const double LabelColumnRatio = 0.4;
        const double FieldHorizontalPadding = 12;

        var (fixture, model, view) = await LoadAtmosphereLightsSectionAsync(LoadTestContentAsync).ConfigureAwait(true);
        var atmosphereSection = (Oxygen.Editor.Controls.PropertiesExpander)view.FindName("AtmosphereLightsSection");
        var sourceCard = atmosphereSection.Items.OfType<Oxygen.Editor.Controls.PropertyCard>().Single();
        _ = atmosphereSection.BringItemIntoView(sourceCard).Should().BeTrue();
        await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var primary = sourceCard.FindDescendants().OfType<Expander>().First();
        primary.IsExpanded = true;
        view.UpdateLayout();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var source = (AtmosphereSourceView)primary.Content;
        _ = source.ActualWidth.Should().BeGreaterThan(340);
        var azimuth = source.FindDescendant<NumberBox>(number => Equals(number.Tag, "SunAzimuth"))!;
        _ = azimuth.ActualWidth.Should().BeGreaterThan(140);
        _ = source.FindDescendant<TextBlock>(text => string.Equals(text.Text, "Azimuth", StringComparison.Ordinal))!.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
        _ = azimuth.LabelPosition.Should().Be(LabelPosition.Left);
        _ = azimuth.Label.Should().Be("Azimuth");
        var valueBorder = azimuth.FindDescendant<Border>(border => string.Equals(border.Name, "PartBackgroundBorder", StringComparison.Ordinal))!;
        _ = valueBorder.ActualWidth.Should().BeGreaterThanOrEqualTo(126);
        _ = (valueBorder.TransformToVisual(azimuth).TransformPoint(default).X - valueBorder.Margin.Left)
            .Should().BeApproximately(((azimuth.ActualWidth - FieldHorizontalPadding) * LabelColumnRatio) + FieldHorizontalPadding, 1);
        var elevation = source.FindDescendant<NumberBox>(number => Equals(number.Tag, "SunElevation"))!;
        var rowPitch = elevation.TransformToVisual(source).TransformPoint(default).Y - azimuth.TransformToVisual(source).TransformPoint(default).Y;
        _ = rowPitch.Should().BeApproximately(azimuth.ActualHeight + 4, 1);
        foreach (var card in source.FindDescendants().OfType<Oxygen.Editor.Controls.PropertyCard>())
        {
            _ = card.Margin.Should().Be(new Thickness(0));
            _ = card.Padding.Should().Be(new Thickness(0));
        }

        var collapsedHeader = primary.FindDescendant<ToggleButton>(toggle => string.Equals(toggle.Name, "ExpanderHeader", StringComparison.Ordinal))!;
        var disclosureGap = source.TransformToVisual(primary).TransformPoint(default).Y
            - collapsedHeader.TransformToVisual(primary).TransformPoint(default).Y - collapsedHeader.ActualHeight;
        _ = disclosureGap.Should().BeApproximately(0, 1);
        await InspectorCapture.HoldIfRequestedAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>Atmosphere reference rows center the picker and its two actions on one line.</summary>
    /// <returns>The reference-row alignment task.</returns>
    [TestMethod]
    public Task AtmosphereReferenceRowsAlignPickerAndActionsOnCenterLine() => EnqueueAsync(async () =>
    {
        var (fixture, model, view) = await LoadAtmosphereLightsSectionAsync(LoadTestContentAsync).ConfigureAwait(true);
        var atmosphereSection = (Oxygen.Editor.Controls.PropertiesExpander)view.FindName("AtmosphereLightsSection");
        var sourceCard = atmosphereSection.Items.OfType<Oxygen.Editor.Controls.PropertyCard>().Single();
        _ = atmosphereSection.BringItemIntoView(sourceCard).Should().BeTrue();
        await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var primary = sourceCard.FindDescendants().OfType<Expander>().First();
        primary.IsExpanded = true;
        view.UpdateLayout();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var references = atmosphereSection.FindDescendants().OfType<ComboBox>().ToArray();
        _ = references.Should().HaveCount(2);
        foreach (var reference in references)
        {
            var row = Grid.GetRow(reference);
            var rowActions = atmosphereSection.FindDescendants().OfType<Button>().Where(button => Grid.GetRow(button) == row && Grid.GetColumn(button) > 0).ToArray();
            _ = rowActions.Should().HaveCount(2);
            var referenceCenter = reference.TransformToVisual(atmosphereSection).TransformPoint(default).Y + (reference.ActualHeight / 2);
            _ = reference.ActualHeight.Should().BeGreaterThanOrEqualTo(32);
            _ = reference.FontSize.Should().Be(14);
            foreach (var action in rowActions)
            {
                var actionCenter = action.TransformToVisual(atmosphereSection).TransformPoint(default).Y + (action.ActualHeight / 2);
                _ = actionCenter.Should().BeApproximately(referenceCenter, 1);
                _ = action.ActualWidth.Should().BeGreaterThanOrEqualTo(32);
                _ = action.ActualHeight.Should().BeGreaterThanOrEqualTo(32);
                var icon = (FontIcon)action.Content;
                var iconCenter = icon.TransformToVisual(atmosphereSection).TransformPoint(default).Y + (icon.ActualHeight / 2);
                _ = iconCenter.Should().BeApproximately(referenceCenter, 1);
            }
        }

        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>Quiet inspector toggles keep a fully transparent background.</summary>
    /// <returns>The toggle styling task.</returns>
    [TestMethod]
    public Task QuietInspectorTogglesUseTransparentBackground() => EnqueueAsync(async () =>
    {
        var (fixture, model, view) = await LoadAtmosphereLightsSectionAsync(LoadTestContentAsync).ConfigureAwait(true);
        var atmosphereSection = (Oxygen.Editor.Controls.PropertiesExpander)view.FindName("AtmosphereLightsSection");
        var sourceCard = atmosphereSection.Items.OfType<Oxygen.Editor.Controls.PropertyCard>().Single();
        _ = atmosphereSection.BringItemIntoView(sourceCard).Should().BeTrue();
        await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var sectionResources = view.Resources;
        var headerToggles = atmosphereSection.FindDescendants().OfType<ToggleButton>().Where(toggle => ReferenceEquals(toggle.Style, sectionResources["QuietInspectorToggle"])).ToArray();
        _ = headerToggles.Should().NotBeEmpty();
        foreach (var toggle in headerToggles)
        {
            _ = ((Microsoft.UI.Xaml.Media.SolidColorBrush)toggle.Background).Color.A.Should().Be(0);
        }

        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>A section header is one full-width disclosure target with a decorative chevron outside its content.</summary>
    /// <returns>The design-contract regression task.</returns>
    [TestMethod]
    public Task SceneSectionHeaderProvidesSingleFullWidthDisclosureTarget() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        var atmosphere = scenario.Section("AtmosphereLightsSection");
        var header = atmosphere.FindDescendant<ToggleButton>(button => string.Equals(button.Name, "ExpanderHeader", StringComparison.Ordinal))!;
        var icon = header.FindDescendant<Viewbox>(element => string.Equals(element.Name, "PartHeaderIconPresenterHolder", StringComparison.Ordinal));
        _ = icon.Should().NotBeNull();
        var chevron = header.FindDescendant<AnimatedIcon>(element => string.Equals(element.Name, "ExpandCollapseChevron", StringComparison.Ordinal))!;
        _ = Microsoft.UI.Xaml.Automation.AutomationProperties.GetAccessibilityView(chevron).Should().Be(Microsoft.UI.Xaml.Automation.Peers.AccessibilityView.Raw);
        _ = chevron.FindAscendant<ContentControl>()!.IsTabStop.Should().BeFalse();
        _ = header.ActualWidth.Should().BeApproximately(atmosphere.ActualWidth, 1);
        var provider = (Microsoft.UI.Xaml.Automation.Provider.IToggleProvider)new Microsoft.UI.Xaml.Automation.Peers.ToggleButtonAutomationPeer(header)
            .GetPattern(Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Toggle);
        provider.Toggle();
        _ = atmosphere.IsExpanded.Should().BeFalse();
        provider.Toggle();
        _ = atmosphere.IsExpanded.Should().BeTrue();
        _ = header.FindDescendants().Should().NotContain((FrameworkElement)atmosphere.Content);
        scenario.AssertCleanHistory();
    });

    /// <summary>The sky section retains the sun disk switch and its four design field labels.</summary>
    /// <returns>The sky-control regression task.</returns>
    [TestMethod]
    public Task SkySectionRetainsSunDiskAndDesignFieldLabels() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        var sky = scenario.Section("SkyAtmosphereSection");
        var sunDisk = SceneCards(sky).Single(card => string.Equals(card.PropertyName, "Sun Disk", StringComparison.Ordinal));
        _ = await scenario.BringIntoViewAsync(sky, sunDisk).ConfigureAwait(true);
        _ = sunDisk.FindDescendant<ToggleSwitch>(control => Equals(control.Tag, "SunDiskEnabled")).Should().NotBeNull();
        _ = SceneFieldLabels(sky).Should().Contain(["Distance scale", "Scattering strength", "Start distance", "Height fog contribution"]);
        _ = sky.Items.OfType<Expander>().Should().ContainSingle(group => Equals(group.Header, "Planet & ground"));
        scenario.AssertCleanHistory();
    });

    /// <summary>The ground albedo swatch edits linear RGB from the display picker and the picked edit undoes.</summary>
    /// <returns>The albedo editing regression task.</returns>
    [TestMethod]
    public Task GroundAlbedoSwatchEditsLinearRgbAndUndoes() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        var sky = scenario.Section("SkyAtmosphereSection");
        await scenario.ExpandGroupAsync("SkyAtmosphereSection", "Planet & ground").ConfigureAwait(true);
        await scenario.SearchAsync("Ground Albedo").ConfigureAwait(true);
        var planet = sky.Items.OfType<Expander>().Single(group => Equals(group.Header, "Planet & ground"));
        _ = await scenario.BringIntoViewAsync(sky, planet).ConfigureAwait(true);
        var albedoField = (Oxygen.Editor.World.Inspector.Controls.InspectorRgbField)await FindInspectorControlAsync(
            (ScrollViewer)scenario.View.FindName("ScenePropertyScroll"),
            () => scenario.View.FindDescendant<Oxygen.Editor.World.Inspector.Controls.InspectorRgbField>(field =>
                field.IsLoaded && Equals(field.Tag, "GroundAlbedo") && ((Oxygen.Editor.Controls.PropertyCard)field.Content).LeadingContent is Button),
            "GroundAlbedo",
            this.TestContext.CancellationToken).ConfigureAwait(true);
        var albedo = (Oxygen.Editor.Controls.PropertyCard)albedoField.Content;
        var swatch = (Button)albedo.LeadingContent!;
        _ = swatch.Flyout.Should().BeOfType<Flyout>();
        scenario.Model.SkyAtmosphere.GroundAlbedoR = AlbedoRedLinear;
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = InspectorRgbPresentation.ToDisplayColor(scenario.Model.SkyAtmosphere.GroundAlbedoColor).R.Should().Be(128);
        _ = ((Microsoft.UI.Xaml.Media.SolidColorBrush)((Border)swatch.Content).Background).Color.R.Should().Be(128);
        var historyCount = scenario.Fixture.Context.History.UndoStack.Count;
        await PickDisplayColorAsync(swatch, Windows.UI.Color.FromArgb(255, 128, 64, 32)).ConfigureAwait(true);
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = scenario.Fixture.Scene.Environment.SkyAtmosphere.GroundAlbedoRgb.Y.Should().BeApproximately(AlbedoGreenLinearPicked, 0.000001f);
        _ = scenario.Fixture.Context.History.UndoStack.Should().HaveCount(historyCount + 1);
        await scenario.Fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = InspectorRgbPresentation.ToDisplayColor(scenario.Model.SkyAtmosphere.GroundAlbedoColor).R.Should().Be(128);
    });

    /// <summary>Read-only numeric text is compact while the editor retains its authored value and mask.</summary>
    /// <param name="value">The numeric value.</param>
    /// <param name="expected">The compact read-only text.</param>
    /// <returns>The native formatting regression task.</returns>
    [TestMethod]
    [DataRow(0f, "0")]
    [DataRow(45f, "45")]
    [DataRow(0.536f, "0.536")]
    [DataRow(-0.536f, "-0.536")]
    public Task SceneNumericDisplayUsesCompactFormatting(float value, string expected) => EnqueueAsync(async () =>
    {
        var view = new EnvironmentView();
        var number = new NumberBox { Mask = "~.###", NumberValue = value, Style = (Style)view.Resources["SceneNumberBoxStyle"] };
        await LoadTestContentAsync(number).ConfigureAwait(true);
        _ = number.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PartValueTextBlock", StringComparison.Ordinal))!.Text.Should().Be(expected);
        _ = number.NumberValue.Should().Be(value);
        _ = number.Mask.Should().Be("~.###");
    });

    /// <summary>The scene number box style is the single owner of the compact input and editor metrics, so layout tests need not re-assert them.</summary>
    /// <returns>The design-metric contract task.</returns>
    [TestMethod]
    public Task SceneNumberBoxCompactStyleFollowsDesignMetrics() => EnqueueAsync(async () =>
    {
        var view = new EnvironmentView();
        var number = new NumberBox { Mask = "~.###", NumberValue = 0.536f, Style = (Style)view.Resources["SceneNumberBoxStyle"] };
        await LoadTestContentAsync(number).ConfigureAwait(true);
        _ = number.CornerRadius.Should().Be(new CornerRadius(4));
        _ = number.BorderThickness.Should().Be(new Thickness(1));
        number.StartEdit();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var editor = number.FindDescendant<TextBox>(text => string.Equals(text.Name, "PartEditBox", StringComparison.Ordinal))!;
        _ = editor.MinHeight.Should().Be(28);
        _ = editor.Padding.Should().Be(new Thickness(6, 4, 6, 4));
        _ = editor.CornerRadius.Should().Be(new CornerRadius(4));
        _ = editor.FontSize.Should().Be(14);
        _ = editor.TextWrapping.Should().Be(TextWrapping.Wrap);
        number.CancelEdit();
    });

    /// <summary>Scene search reveals the inactive auto exposure minimum with its applicability note.</summary>
    /// <returns>The search-reveal regression task.</returns>
    [TestMethod]
    public Task SceneSearchRevealsInactiveAutoFieldWithApplicabilityNote() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, width: null, height: null, host: ScenarioHost.Scroll).ConfigureAwait(true);
        var exposure = scenario.Section("ExposureSection");
        exposure.IsExpanded = false;
        var autoMinimum = (Oxygen.Editor.Controls.PropertyCard)scenario.Element<Oxygen.Editor.Controls.InspectorNumberField>("AutoExposureMinEvCard").Content;

        // precondition (was asserted mid-test): manual exposure keeps the auto minimum inactive
        _ = scenario.Model.Exposure.ExposureMode.Should().Be(Oxygen.Editor.World.Serialization.ExposureMode.Manual);
        _ = scenario.Named("AutoExposureMinEvCard").Visibility.Should().Be(Visibility.Collapsed);

        await scenario.SearchAsync("auto_exposure_min_ev").ConfigureAwait(true);
        _ = scenario.Named("AutoExposureMinEvCard").Visibility.Should().Be(Visibility.Visible);
        _ = ((StackPanel)autoMinimum.Content).Children.OfType<TextBlock>().Should().Contain(element => element.Text.Contains(AppliesInAutoExposureModeCopy, StringComparison.Ordinal));
        _ = exposure.IsExpanded.Should().BeTrue();
    });

    /// <summary>The scope selector filters sections by browsing scope and the no-match indicator tracks query hits.</summary>
    /// <returns>The scope-filtering regression task.</returns>
    [TestMethod]
    public Task SceneScopeSelectorFiltersSectionsAndShowsNoMatchIndicator() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, width: null, height: null, host: ScenarioHost.Scroll).ConfigureAwait(true);
        var exposure = scenario.Section("ExposureSection");
        var sky = scenario.Section("SkyAtmosphereSection");
        var background = scenario.Section("BackgroundSection");
        var references = (FrameworkElement)scenario.View.FindName("SceneReferencesView");

        // collapsed baseline: search restore cycles must return the section to this state
        exposure.IsExpanded = false;

        await scenario.SelectScopeAsync("Post-processing").ConfigureAwait(true);
        _ = sky.Visibility.Should().Be(Visibility.Collapsed);
        _ = background.Visibility.Should().Be(Visibility.Collapsed);
        _ = exposure.Visibility.Should().Be(Visibility.Visible);
        _ = references.Visibility.Should().Be(Visibility.Collapsed);

        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        await scenario.SelectScopeAsync("References").ConfigureAwait(true);
        _ = references.Visibility.Should().Be(Visibility.Visible);
        _ = sky.Visibility.Should().Be(Visibility.Collapsed);

        await scenario.SearchAsync("script").ConfigureAwait(true);
        _ = references.Visibility.Should().Be(Visibility.Visible);
        _ = ((TextBlock)scenario.View.FindName("NoScenePropertyMatches")).Visibility.Should().Be(Visibility.Collapsed);
        await scenario.SearchAsync("no-such-reference").ConfigureAwait(true);
        _ = references.Visibility.Should().Be(Visibility.Collapsed);
        _ = ((TextBlock)scenario.View.FindName("NoScenePropertyMatches")).Visibility.Should().Be(Visibility.Visible);

        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        await scenario.SelectScopeAsync("Post-processing").ConfigureAwait(true);
        await scenario.SearchAsync("manual").ConfigureAwait(true);
        _ = scenario.Named("AutoExposureMinEvCard").Visibility.Should().Be(Visibility.Collapsed);
        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        _ = exposure.IsExpanded.Should().BeFalse();
        _ = background.Visibility.Should().Be(Visibility.Collapsed);

        await scenario.SelectScopeAsync("All").ConfigureAwait(true);
        await scenario.SearchAsync("background").ConfigureAwait(true);
        _ = background.Visibility.Should().Be(Visibility.Visible);
        _ = sky.Visibility.Should().Be(Visibility.Collapsed);
        _ = exposure.Visibility.Should().Be(Visibility.Collapsed);
        await scenario.SearchAsync("no-such-property").ConfigureAwait(true);
        _ = background.Visibility.Should().Be(Visibility.Collapsed);
        _ = ((TextBlock)scenario.View.FindName("NoScenePropertyMatches")).Visibility.Should().Be(Visibility.Visible);
        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        _ = background.Visibility.Should().Be(Visibility.Visible);
    });

    /// <summary>The display gamma card stays visible and labeled when tone mapping is set to None.</summary>
    /// <returns>The tone-mapping applicability regression task.</returns>
    [TestMethod]
    public Task DisplayGammaCardAppearsWhenToneMappingIsNone() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        scenario.Model.PostProcessing.ToneMapping = Oxygen.Editor.World.Serialization.ToneMappingMode.None;
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = scenario.Named("DisplayGammaCard").Visibility.Should().Be(Visibility.Visible);
        _ = scenario.Element<Oxygen.Editor.Controls.InspectorNumberField>("DisplayGammaCard").Label.Should().Be("Display Gamma");
    });

    /// <summary>Scene extra-asset references are added and removed through the interactive inspector.</summary>
    /// <returns>The UI authoring regression task.</returns>
    [TestMethod]
    public Task SceneExtraAssetReferencesCanBeAddedAndRemovedInteractively() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, host: ScenarioHost.Scroll).ConfigureAwait(true);

        var referencesView = (SceneReferencesSectionView)scenario.View.FindName("SceneReferencesView");
        var section = referencesView.FindDescendant<Oxygen.Editor.Controls.PropertiesExpander>()!;
        var extraAssetCard = section.Items.OfType<Oxygen.Editor.Controls.PropertyCard>()
            .Single(card => string.Equals(card.PropertyName, "Extra Asset", StringComparison.Ordinal));
        _ = section.BringItemIntoView(extraAssetCard).Should().BeTrue();

        const string path = "/Content/Materials/Shared.omat";
        var pathInput = extraAssetCard.FindDescendants().OfType<TextBox>().Single();
        pathInput.Text = path;
        await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

        // Bounded realization wait: a single render pass can miss the button while the repeater materializes it (recorded flake).
        var add = await WaitForDescendantAsync(section, (Button button) => Equals(button.Content, "Add Extra Asset"), "add-extra-asset button", this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = add.IsEnabled.Should().BeTrue();
        ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(add)
            .GetPattern(Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = scenario.Fixture.Scene.References.ExtraAssets.Should().ContainSingle().Which.Should().Be(path);
        _ = scenario.Fixture.Context.Metadata.IsDirty.Should().BeTrue();

        var remove = await WaitForDescendantAsync(section, (Button button) => Equals(button.Content, "Remove"), "remove-extra-asset button", this.TestContext.CancellationToken).ConfigureAwait(true);
        ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(remove)
            .GetPattern(Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = scenario.Fixture.Scene.References.ExtraAssets.Should().BeEmpty();
        _ = scenario.Fixture.Context.History.UndoStack.Should().HaveCount(2);
    });

    /// <summary>The eight design-order sections preserve all 47 scene cards and closed secondary groups.</summary>
    /// <returns>The design-inventory regression task.</returns>
    [TestMethod]
    public Task SceneSectionsFollowDesignInventory() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var sections = ((StackPanel)scenario.View.FindName("SceneSections")).Children.SelectMany(child => child switch
        {
            UserControl { Content: Oxygen.Editor.Controls.PropertiesExpander section } => [section],
            UserControl { Content: StackPanel effects } => effects.Children.OfType<Oxygen.Editor.Controls.PropertiesExpander>(),
            _ => Enumerable.Empty<Oxygen.Editor.Controls.PropertiesExpander>(),
        }).ToArray();
        _ = sections.Select(section => section.Header).Should().Equal("Scene References", "Atmosphere Lights", "Sky Atmosphere", "Background", "Exposure", "Tone Mapping", "Color Grading", "Bloom");
        _ = sections.SelectMany(SceneCards).Should().HaveCount(47);
        _ = sections.Select(section => section.IsExpanded).Should().Equal(true, true, true, true, true, true, false, false);
        var sky = sections[2];
        var skyGroups = sky.Items.OfType<Expander>().ToArray();
        _ = skyGroups.Select(group => group.Header).Should().Equal("Planet & ground", "Scattering", "Aerial perspective");
        _ = skyGroups.Select(group => group.IsExpanded).Should().OnlyContain(expanded => !expanded);
        _ = skyGroups[0].IsExpanded.Should().BeFalse();
        AssertDisclosureCardOrder(skyGroups[0], "PlanetRadiusKm", "AtmosphereHeightKm", "GroundAlbedo");
        AssertDisclosureCardOrder(skyGroups[1], "RayleighScaleHeightKm", "MieScaleHeightKm", "MieAnisotropy");
        AssertDisclosureCardOrder(skyGroups[2], "AerialPerspectiveDistanceScale", "AerialScatteringStrength", "AerialPerspectiveStartDepthMeters", "HeightFogContribution");

        var exposure = sections.Single(section => Equals(section.Header, "Exposure"));
        var exposureGroups = exposure.Items.OfType<Expander>().ToArray();
        _ = exposureGroups.Select(group => group.Header).Should().Equal("Metering & limits", "Adaptation", "Histogram & calibration", "Exposure shaping");
        _ = exposureGroups.Select(group => group.IsExpanded).Should().Equal(true, false, false, false);
        AssertDisclosureCardOrder(exposureGroups[0], "AutoExposureMeteringMode", "AutoExposureMinEv", "AutoExposureMaxEv", "AutoExposureTargetLuminance", "AutoExposureSpotMeterRadius");
        AssertDisclosureCardOrder(exposureGroups[1], "AutoExposureSpeedUp", "AutoExposureSpeedDown", "AutoExposureTransitionDistanceEv");
        AssertDisclosureCardOrder(exposureGroups[2], "ExposureKey", "AutoExposureLowPercentile", "AutoExposureHighPercentile", "AutoExposureMinLogLuminance", "AutoExposureLogLuminanceRange", "AutoExposureBlackInfluence");
        AssertDisclosureCardOrder(exposureGroups[3], "AutoExposureMeteringMask", "AutoExposureCompensationCurve");
        scenario.AssertCleanHistory();
    });

    /// <summary>A disclosure header toggles through three cycles, keeps its chevron and content in step, and restores its original state.</summary>
    /// <param name="automationId">The disclosure's automation identity.</param>
    /// <param name="query">A search query that reveals a field inside the disclosure.</param>
    /// <param name="sectionName">The section namescope identity owning the disclosure.</param>
    /// <returns>The disclosure-toggle regression task.</returns>
    [TestMethod]
    [DataRow("Scene.Sky.PlanetGround", "PlanetRadiusKm", "SkyAtmosphereSection")]
    [DataRow("Scene.Sky.Scattering", "RayleighScaleHeightKm", "SkyAtmosphereSection")]
    [DataRow("Scene.Sky.AerialPerspective", "AerialPerspectiveDistanceScale", "SkyAtmosphereSection")]
    [DataRow("Scene.Exposure.MeteringLimits", "AutoExposureMinEv", "ExposureSection")]
    [DataRow("Scene.Exposure.Adaptation", "AutoExposureSpeedUp", "ExposureSection")]
    [DataRow("Scene.Exposure.HistogramCalibration", "ExposureKey", "ExposureSection")]
    [DataRow("Scene.Exposure.ExposureShaping", "AutoExposureCompensationCurve", "ExposureSection")]
    public Task SceneDisclosureHeaderTogglesThroughThreeCyclesAndRestores(string automationId, string query, string sectionName) => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var owner = scenario.Section(sectionName);
        var item = owner.Items.OfType<Expander>().Single(expander => string.Equals(AutomationProperties.GetAutomationId(expander), automationId, StringComparison.Ordinal));
        await scenario.SearchAsync(query).ConfigureAwait(true);
        owner.StartBringIntoView();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(owner.UpdateLayout).ConfigureAwait(true);
        _ = owner.BringItemIntoView(item).Should().BeTrue();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(owner.UpdateLayout).ConfigureAwait(true);
        var scroller = (ScrollViewer)scenario.View.FindName("ScenePropertyScroll");
        var group = (Expander)await FindInspectorControlAsync(scroller, () => owner.FindDescendant<Expander>(candidate =>
            candidate.ActualHeight > 0 && string.Equals(AutomationProperties.GetAutomationId(candidate), automationId, StringComparison.Ordinal)),
            automationId,
            this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = group.ApplyTemplate();
        var header = group.FindDescendant<ToggleButton>(toggle => string.Equals(toggle.Name, "ExpanderHeader", StringComparison.Ordinal))!;
        _ = header.ApplyTemplate();

        // the QuietDisclosure template names its header chevron
        var chevron = header.FindDescendant<FontIcon>(icon => string.Equals(icon.Name, "DisclosureChevron", StringComparison.Ordinal))!;
        var content = group.FindDescendant<Border>(border => string.Equals(border.Name, "ExpanderContent", StringComparison.Ordinal))!;
        var originallyExpanded = group.IsExpanded;
        _ = header.IsTabStop.Should().BeTrue();
        _ = header.UseSystemFocusVisuals.Should().BeTrue();

        for (var cycle = 0; cycle < 3; cycle++)
        {
            var expectedExpanded = !group.IsExpanded;
            Toggle(header);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            _ = group.IsExpanded.Should().Be(expectedExpanded);
            _ = chevron.Glyph.Should().Be(expectedExpanded ? ChevronExpandedGlyph : ChevronCollapsedGlyph);
            _ = content.Visibility.Should().Be(expectedExpanded ? Visibility.Visible : Visibility.Collapsed);
        }

        if (group.IsExpanded != originallyExpanded)
        {
            Toggle(header);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }

        scenario.AssertCleanHistory();
    });

    /// <summary>Scene search expands the nested planet disclosure to reveal its card and restores it collapsed when cleared.</summary>
    /// <returns>The nested-disclosure restore regression task.</returns>
    [TestMethod]
    public Task SceneSearchExpandsNestedDisclosureAndRestoresCollapsedState() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var sky = scenario.Section("SkyAtmosphereSection");
        var planet = sky.Items.OfType<Expander>().Single(group => Equals(group.Header, "Planet & ground"));
        await scenario.SearchAsync("ground_albedo").ConfigureAwait(true);
        _ = planet.IsExpanded.Should().BeTrue();
        _ = scenario.Named("GroundAlbedoCard").Visibility.Should().Be(Visibility.Visible);
        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        _ = planet.IsExpanded.Should().BeFalse();
        scenario.AssertCleanHistory();
    });

    [TestMethod]
    public Task SceneSearchKeepsFieldContentAndStoredValuesWhileApplicabilityChanges() => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync, host: ScenarioHost.None).ConfigureAwait(true);
        var exposure = scenario.Section("ExposureSection");
        var spot = (Oxygen.Editor.Controls.PropertyCard)scenario.Element<Oxygen.Editor.Controls.InspectorNumberField>("AutoExposureSpotMeterRadiusCard").Content;
        var originalContent = spot.Content;
        var storedRadius = scenario.Fixture.Scene.Environment.PostProcess.AutoExposureSpotMeterRadius;
        await scenario.SearchAsync("spot radius").ConfigureAwait(true);
        _ = scenario.Named("AutoExposureSpotMeterRadiusCard").Visibility.Should().Be(Visibility.Visible);
        _ = ((StackPanel)spot.Content).Children.OfType<TextBlock>().Should().Contain(note =>
            note.Text.Contains(AppliesInAutoExposureSpotMeteringCopy, StringComparison.Ordinal));
        _ = spot.Content.Should().BeSameAs(originalContent);
        _ = scenario.Fixture.Context.Metadata.IsDirty.Should().BeFalse();
        _ = scenario.Fixture.Context.History.UndoStack.Should().BeEmpty();

        scenario.Model.Exposure.ExposureMode = Oxygen.Editor.World.Serialization.ExposureMode.Auto;
        scenario.Model.Exposure.AutoExposureMeteringMode = Oxygen.Editor.World.Serialization.MeteringMode.Spot;
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = ((StackPanel)spot.Content).Children.OfType<TextBlock>().Should().NotContain(note =>
            note.Visibility == Visibility.Visible && note.Text.StartsWith(StoredValueNotePrefixCopy, StringComparison.Ordinal));
        await scenario.SearchAsync("spot radius missing-field").ConfigureAwait(true);
        _ = ((TextBlock)scenario.View.FindName("NoScenePropertyMatches")).Visibility.Should().Be(Visibility.Visible);
        await scenario.SearchAsync(string.Empty).ConfigureAwait(true);
        _ = spot.Content.Should().BeSameAs(originalContent);
        _ = scenario.Fixture.Scene.Environment.PostProcess.AutoExposureSpotMeterRadius.Should().Be(storedRadius);
    });

    /// <summary>Swapping the view onto a replacement model detaches the previous one: later edits to it cannot move the view.</summary>
    /// <returns>The observer-ownership regression task.</returns>
    [TestMethod]
    public Task SceneSearchObservesReplacementModelWithoutRetainingThePreviousObserver() => EnqueueAsync(async () =>
    {
        using var first = new SceneAuthoringFixture();
        using var second = new SceneAuthoringFixture();
        using var firstModel = (EnvironmentViewModel)CreateModel("Environment", first);
        using var secondModel = (EnvironmentViewModel)CreateModel("Environment", second);
        var view = new EnvironmentView { ViewModel = firstModel, Width = 420, Height = 780 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        view.ViewModel = secondModel;
        await WaitForRenderAsync().ConfigureAwait(true);
        var search = (TextBox)view.FindName("ScenePropertySearchBox");
        search.Text = "auto_exposure_min_ev";
        await WaitForRenderAsync().ConfigureAwait(true);
        var field = (Oxygen.Editor.Controls.InspectorNumberField)FindInspectorElement(view, "AutoExposureMinEvCard");
        var minimum = (Oxygen.Editor.Controls.PropertyCard)field.Content;

        // The replacement model is still Manual: the stored-value note names that mode explicitly,
        // so a stale observer applying the first model's switch to Auto would visibly rewrite it.
        _ = ((StackPanel)minimum.Content).Children.OfType<TextBlock>().Should().Contain(note =>
            note.Visibility == Visibility.Visible
            && note.Text.StartsWith(StoredValueNotePrefixCopy, StringComparison.Ordinal)
            && note.Text.Contains("Current mode: Manual", StringComparison.Ordinal));
        string[] ObservedState() => new[] { field.Visibility.ToString() }
            .Concat(((StackPanel)minimum.Content).Children.OfType<TextBlock>().Select(note => note.Text + "|" + note.Visibility))
            .ToArray();
        var observedBefore = ObservedState();

        // A stale observer on the previous model would push this applicability change into the view.
        firstModel.Exposure.ExposureMode = Oxygen.Editor.World.Serialization.ExposureMode.Auto;
        await firstModel.PendingEdits.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = firstModel.Exposure.ExposureMode.Should().Be(Oxygen.Editor.World.Serialization.ExposureMode.Auto);
        _ = view.ViewModel.Should().BeSameAs(secondModel);
        _ = ObservedState().Should().Equal(observedBefore);
        _ = second.Scene.Environment.PostProcess.AutoExposureMinEv.Should().Be(-6);
    });

    /// <summary>The realized near-plane editor rejects an invalid commit and displays current inline feedback.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task CameraTextCommitRejectsInvalidNearPlaneAndClearsItsDiagnosticOnCorrection() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (PerspectiveCameraViewModel)CreateModel("Camera", fixture);
        var view = new PerspectiveCameraView
        {
            ViewModel = model,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, "NearPlane"))!;
        var field = number.FindAscendant<Oxygen.Editor.Controls.InspectorNumberField>()!;
        var before = fixture.Camera.NearPlane;

        // Invalid by the fixture contract: twice the far plane can never be a valid near plane.
        var invalidNearPlane = (fixture.Camera.FarPlane * 2f).ToString(CultureInfo.InvariantCulture);
        await EnterTextAsync(number, invalidNearPlane).ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Camera.NearPlane.Should().Be(before);
        _ = number.NumberValue.Should().Be(before);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = model.NearPlaneDiagnostic.Message.Should().NotBeEmpty();
        _ = field.HasError.Should().BeTrue();
        _ = field.ErrorText.Should().Be(model.NearPlaneDiagnostic.Message);
        await EnterTextAsync(number, "2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Camera.NearPlane.Should().Be(2);
        _ = model.NearPlaneDiagnostic.Message.Should().BeEmpty();
        _ = field.HasError.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
    });

    /// <summary>A zero scale axis shows scoped inline feedback without changing the model or history.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task TransformTextValidationPreservesScaleUntilAValidCommit() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (TransformViewModel)CreateModel("Transform", fixture);
        var view = new TransformView
        {
            ViewModel = model,
        };
        var scroller = new ScrollViewer
        {
            Content = view,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var card = view.FindDescendant<Oxygen.Editor.Controls.PropertyCard>(element => string.Equals(element.PropertyName, "Scale", StringComparison.Ordinal))!;
        card.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false });
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        var number = card.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        var diagnostic = (TextBlock)view.FindName("ScaleXDiagnosticText")!;
        await EnterTextAsync(number, "0").ConfigureAwait(true);

        // Live validation: the diagnostic surfaces while typing, before any commit.
        _ = fixture.Node.Components.OfType<TransformComponent>().Single().LocalScale.X.Should().Be(1);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = model.ScaleXDiagnostic.Message.Should().NotBeEmpty();
        _ = diagnostic.Visibility.Should().Be(Visibility.Visible);
        _ = diagnostic.Text.Should().Be(model.ScaleXDiagnostic.Message);
        number.CompletePendingTextEdit();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);

        // Commit parity with the camera inspector: committing the rejected value changes nothing.
        _ = fixture.Node.Components.OfType<TransformComponent>().Single().LocalScale.X.Should().Be(1);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = model.ScaleXDiagnostic.Message.Should().NotBeEmpty();
        await EnterTextAsync(number, "2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        _ = fixture.Node.Components.OfType<TransformComponent>().Single().LocalScale.X.Should().Be(2);
        _ = model.ScaleXDiagnostic.Message.Should().BeEmpty();
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
    });

    /// <summary>Vector child-control events traverse the view and group a hundred samples into one entry.</summary>
    /// <param name="field">The environment vector control.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("GroundAlbedo")]
    [DataRow("SkyLuminance")]
    [DataRow("BackgroundColor")]
    public Task EnvironmentVectorControlGroupsSamplesAndUndoRefreshesTheControl(string field) => EnqueueAsync(async () =>
    {
        using var scenario = await EnvironmentInspectorScenario.LoadAsync(LoadTestContentAsync).ConfigureAwait(true);
        if (VectorFieldPreparations.TryGetValue(field, out var prepare))
        {
            await prepare(scenario).ConfigureAwait(true);
        }

        var propertyScroll = (ScrollViewer)scenario.View.FindName("ScenePropertyScroll");
        var vector = await FindVisibleVectorAsync(scenario.View, propertyScroll, field).ConfigureAwait(true);
        var number = vector.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        var before = number.NumberValue;
        number.OnEditSessionStarted(NumberBoxEditInteractionKind.PointerDrag);
        for (var sample = 1; sample <= 100; sample++)
        {
            number.NumberValue = sample / 200f;
        }

        number.OnEditSessionCompleted(NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await scenario.Model.PendingEdits.ConfigureAwait(true);
        _ = scenario.Fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = number.NumberValue.Should().Be(0.5f);
        await scenario.Fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before);
    });

    private static async Task<(SceneAuthoringFixture fixture, EnvironmentViewModel model, AtmosphereLightsSectionView view)> LoadAtmosphereLightsSectionAsync(
        Func<FrameworkElement, Task> loadContent,
        double width = 420)
    {
        var fixture = new SceneAuthoringFixture();
        fixture.Node.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary;
        var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new AtmosphereLightsSectionView { ViewModel = model.AtmosphereLights };
        var host = new Grid { Width = width, Height = 780, RequestedTheme = ElementTheme.Dark, Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Windows.UI.Color.FromArgb(255, 37, 40, 43)) };
        host.Resources.MergedDictionaries.Add(new ResourceDictionary { Source = new Uri("ms-appx:///Microsoft.UI.Xaml/DensityStyles/Compact.xaml") });
        host.Children.Add(view);
        await loadContent(host).ConfigureAwait(true);
        return (fixture, model, view);
    }

    private async Task AssertInspectorRealizesAsync(
        Func<SceneAuthoringFixture, IDisposable> createModel,
        Func<object, UserControl> createView,
        Action<SceneAuthoringFixture>? prepareFixture,
        Func<UserControl, Task>? assertRealizedAsync)
    {
        await EnqueueAsync(async () =>
        {
            using var fixture = new SceneAuthoringFixture();
            prepareFixture?.Invoke(fixture);
            using var model = createModel(fixture);
            var view = createView(model);
            await LoadTestContentAsync(view).ConfigureAwait(true);
            if (assertRealizedAsync is null)
            {
                _ = view.FindDescendant<NumberBox>().Should().NotBeNull();
                return;
            }

            await assertRealizedAsync(view).ConfigureAwait(true);
        }).ConfigureAwait(true);
    }
}
