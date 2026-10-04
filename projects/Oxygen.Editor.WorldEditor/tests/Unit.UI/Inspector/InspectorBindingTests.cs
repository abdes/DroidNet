// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorModels;
using NumberBox = DroidNet.Controls.NumberBox;
using Expander = Microsoft.UI.Xaml.Controls.Expander;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
public sealed partial class InspectorBindingTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Each inspector realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <param name="kind">The inspector to realize.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Transform")]
    [DataRow("Camera")]
    [DataRow("Light")]
    [DataRow("Environment")]
    public Task InspectorXamlLoadsItsNumericControls(string kind) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        if (kind == "Environment")
        {
            fixture.Node.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary;
        }

        using var model = CreateModel(kind, fixture);
        var view = kind switch
        {
            "Transform" => (UserControl)new TransformView
            {
                ViewModel = (TransformViewModel)model
            },
            "Camera" => new PerspectiveCameraView
            {
                ViewModel = (PerspectiveCameraViewModel)model
            },
            "Light" => new DirectionalLightView
            {
                ViewModel = (DirectionalLightViewModel)model
            },
            "Environment" => new EnvironmentView
            {
                ViewModel = (EnvironmentViewModel)model
            },
            "Material" => new MaterialEditorView
            {
                ViewModel = (MaterialEditorViewModel)model
            },
            _ => throw new ArgumentOutOfRangeException(nameof(kind)),
        };
        if (view is EnvironmentView environmentView)
        {
            ((Expander)FindInspectorElement(environmentView, "PrimarySourceDisclosure")).IsExpanded = true;
        }

        await LoadTestContentAsync(view).ConfigureAwait(true);
        if (view is MaterialEditorView)
        {
            var scroller = view.FindDescendant<ScrollViewer>()!;
            _ = await FindInspectorControlAsync(scroller, () => view.FindDescendant<NumberBox>(number => Equals(number.Tag, "RoughnessFactor")), "Material.RoughnessFactor", this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = view.KeyboardAccelerators.Should().OnlyContain(accelerator => ReferenceEquals(accelerator.ScopeOwner, view));
        }
        else
        {
            _ = view.FindDescendant<NumberBox>().Should().NotBeNull();
        }
    });

    /// <summary>Sky luminance uses a single-line header and full-width RGB multiplier editor at both dock widths.</summary>
    /// <param name="width">The available inspector width.</param>
    /// <returns>The native layout regression task.</returns>
    [TestMethod]
    [DataRow(350d)]
    [DataRow(260d)]
    public Task SkyLuminanceUsesTwoRowRgbLayout(double width) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model };
        var host = new Grid { Width = width, Height = 780 };
        host.Children.Add(view);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        ((TextBox)view.FindName("ScenePropertySearchBox")).Text = "sky_luminance_factor_rgb";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var sky = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "SkyAtmosphereSection");
        var card = SceneCards(sky).Single(property => property.PropertyName == "Sky Luminance");
        _ = sky.BringItemIntoView(card);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var label = card.FindDescendant<TextBlock>(text => text.Text == card.PropertyName)!;
        var unit = card.FindDescendant<TextBlock>(text => text.Text == "RGB ×")!;
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
        foreach (var input in inputs)
        {
            var component = input.Name switch { "PartNumberBoxX" => "X", "PartNumberBoxY" => "Y", _ => "Z" };
            var expectedColor = component switch { "X" => Microsoft.UI.Colors.Red, "Y" => Microsoft.UI.Colors.Green, _ => Microsoft.UI.Colors.Blue };
            var compactLabel = input.FindDescendant<TextBlock>(text => text.Name == "PartCompactLabelTextBlock")!;
            _ = input.IsCompact.Should().BeTrue();
            _ = compactLabel.Visibility.Should().Be(Visibility.Visible);
            _ = compactLabel.Text.Should().Be(channels.ComponentLabels[component]);
            _ = ((Microsoft.UI.Xaml.Media.SolidColorBrush)compactLabel.Foreground).Color.Should().Be(expectedColor);
            _ = input.CornerRadius.Should().Be(new CornerRadius(4));
            _ = input.BorderThickness.Should().Be(new Thickness(1));
            var editor = input.FindDescendant<TextBox>(text => text.Name == "PartEditBox")!;
            _ = editor.MinHeight.Should().Be(28);
            _ = editor.Padding.Should().Be(new Thickness(6, 4, 6, 4));
            _ = editor.CornerRadius.Should().Be(new CornerRadius(4));
            _ = editor.FontSize.Should().Be(14);
            var beforeWidth = channels.ActualWidth;
            var beforeHeight = channels.ActualHeight;
            var beforePosition = input.TransformToVisual(card).TransformPoint(default);
            RaiseNumberEvent(input, "StartEdit");
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            _ = editor.Visibility.Should().Be(Visibility.Visible);
            _ = editor.Focus(FocusState.Programmatic).Should().BeTrue();
            _ = editor.TextWrapping.Should().Be(TextWrapping.Wrap);
            _ = editor.FindDescendants().OfType<Button>().Should().NotContain(button => button.Visibility == Visibility.Visible && button.ActualWidth > 0);
            _ = channels.ActualWidth.Should().BeApproximately(beforeWidth, 1);
            _ = channels.ActualHeight.Should().BeApproximately(beforeHeight, 1);
            _ = input.TransformToVisual(card).TransformPoint(default).X.Should().BeApproximately(beforePosition.X, 1);
            _ = input.TransformToVisual(card).TransformPoint(default).Y.Should().BeApproximately(beforePosition.Y, 1);
            if (width == 350 && component == "X" && int.TryParse(Environment.GetEnvironmentVariable("OXYGEN_UI_CAPTURE_HOLD_SECONDS"), out var captureSeconds) && captureSeconds > 0)
            {
                await Task.Delay(TimeSpan.FromSeconds(captureSeconds), this.TestContext.CancellationToken).ConfigureAwait(true);
            }
            RaiseNumberEvent(input, "CancelEdit");
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }
        _ = card.FindDescendants().OfType<ColorPicker>().Should().BeEmpty();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await InspectorCapture.SaveIfRequestedAsync(card, $"sky-luminance-{width}").ConfigureAwait(true);
    });

    /// <summary>Background color uses a full-width two-row editor with visible colored channels.</summary>
    /// <param name="width">The available inspector width.</param>
    /// <param name="theme">The editor theme.</param>
    /// <returns>The native color layout regression task.</returns>
    [TestMethod]
    [DataRow(350d, ElementTheme.Light)]
    [DataRow(350d, ElementTheme.Dark)]
    [DataRow(260d, ElementTheme.Light)]
    [DataRow(260d, ElementTheme.Dark)]
    public Task BackgroundColorUsesTwoRowRgbLayout(double width, ElementTheme theme) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model };
        var host = new Grid { Width = width, Height = 780, RequestedTheme = theme };
        host.Children.Add(view);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        ((TextBox)view.FindName("ScenePropertySearchBox")).Text = "background";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var section = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "BackgroundSection");
        _ = section.Description.Should().Be("Fallback color when atmosphere rendering is disabled.");
        _ = ((Button)FindInspectorElement(view, "ResetBackgroundButton")).IsEnabled.Should().BeTrue();
        var card = SceneCards(section).Single(property => property.PropertyName == "Color");
        _ = section.BringItemIntoView(FindInspectorElement(view, "BackgroundColorCard"));
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var label = card.FindDescendant<TextBlock>(text => text.Text == "Color")!;
        var unit = card.FindDescendant<TextBlock>(text => text.Text == "Linear RGB")!;
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
        foreach (var input in inputs)
        {
            var expectedColor = input.Name switch { "PartNumberBoxX" => Microsoft.UI.Colors.Red, "PartNumberBoxY" => Microsoft.UI.Colors.Green, _ => Microsoft.UI.Colors.Blue };
            var compactLabel = input.FindDescendant<TextBlock>(text => text.Name == "PartCompactLabelTextBlock")!;
            var value = input.FindDescendant<TextBlock>(text => text.Name == "PartValueTextBlock")!;
            _ = input.ActualWidth.Should().BeGreaterThan(50);
            _ = input.ActualHeight.Should().BeApproximately(32, 1);
            _ = input.CornerRadius.Should().Be(new CornerRadius(4));
            _ = input.BorderThickness.Should().Be(new Thickness(1));
            _ = compactLabel.Visibility.Should().Be(Visibility.Visible);
            _ = compactLabel.Text.Should().Be(input.Name switch { "PartNumberBoxX" => "R", "PartNumberBoxY" => "G", _ => "B" });
            _ = ((Microsoft.UI.Xaml.Media.SolidColorBrush)compactLabel.Foreground).Color.Should().Be(expectedColor);
            _ = value.Text.Should().Be("0");
            _ = value.Visibility.Should().Be(Visibility.Visible);
            _ = value.Opacity.Should().Be(1);
            _ = value.ActualWidth.Should().BeGreaterThan(value.Padding.Left + value.Padding.Right);
            _ = value.ActualWidth.Should().BeGreaterThanOrEqualTo(value.DesiredSize.Width - 1);
            _ = (value.TransformToVisual(input).TransformPoint(default).X + value.ActualWidth).Should().BeLessThanOrEqualTo(input.ActualWidth + 1);
            var beforeWidth = input.ActualWidth;
            var beforeHeight = input.ActualHeight;
            RaiseNumberEvent(input, "StartEdit");
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            var editor = input.FindDescendant<TextBox>(text => text.Name == "PartEditBox")!;
            _ = editor.Visibility.Should().Be(Visibility.Visible);
            _ = editor.Text.Should().Be("0");
            _ = editor.FindDescendants().OfType<Button>().Should().NotContain(button => button.Visibility == Visibility.Visible && button.ActualWidth > 0);
            _ = input.ActualWidth.Should().BeApproximately(beforeWidth, 1);
            _ = input.ActualHeight.Should().BeApproximately(beforeHeight, 1);
            RaiseNumberEvent(input, "CancelEdit");
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }

        if (int.TryParse(Environment.GetEnvironmentVariable("OXYGEN_UI_CAPTURE_HOLD_SECONDS"), out var captureSeconds) && captureSeconds > 0)
        {
            await Task.Delay(TimeSpan.FromSeconds(captureSeconds), this.TestContext.CancellationToken).ConfigureAwait(true);
        }
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await InspectorCapture.SaveIfRequestedAsync(section, $"background-{width}-{theme}").ConfigureAwait(true);
    });

    /// <summary>The source panel uses the whole section width and scopes fit without clipping.</summary>
    [TestMethod]
    public Task AtmosphereScalarUnitsUseCompactFieldsAndFourPixelGaps() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model };
        var host = new Grid { Width = 350, Height = 780 };
        host.Children.Add(view);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        ((TextBox)view.FindName("ScenePropertySearchBox")).Text = "planet_radius";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(input => Equals(input.Tag, "PlanetRadiusKm"))!;
        var suffix = number.FindDescendant<TextBlock>(text => text.Name == "PartValueQualifier")!;
        var field = number.FindDescendant<Border>(border => border.Name == "PartBackgroundBorder")!;
        _ = number.Label.Should().NotBeNullOrEmpty();
        _ = field.ActualHeight.Should().BeApproximately(30, 1);
        _ = number.ActualWidth.Should().BeGreaterThan(100);
        _ = number.CornerRadius.Should().Be(new CornerRadius(4));
        var gap = suffix.TransformToVisual(view).TransformPoint(default).X
            - field.TransformToVisual(view).TransformPoint(default).X - field.ActualWidth - field.Margin.Right;
        _ = gap.Should().BeApproximately(4, 1);
        var beforeHeight = number.ActualHeight;
        var beforeWidth = number.ActualWidth;
        RaiseNumberEvent(number, "StartEdit");
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = number.ActualHeight.Should().BeApproximately(beforeHeight, 1);
        _ = number.ActualWidth.Should().BeApproximately(beforeWidth, 1);
        RaiseNumberEvent(number, "CancelEdit");
    });

    /// <summary>The source panel uses the whole section width and scopes fit without clipping.</summary>
    /// <returns>The UI layout regression task.</returns>
    [TestMethod]
    public Task SceneSourceLayoutUsesFullWidthAndQuietDisclosures() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        fixture.Node.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary;
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model };
        var scroller = new Grid { Width = 420, Height = 780, RequestedTheme = ElementTheme.Dark, Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Windows.UI.Color.FromArgb(255, 37, 40, 43)) };
        scroller.Resources.MergedDictionaries.Add(new ResourceDictionary { Source = new Uri("ms-appx:///Microsoft.UI.Xaml/DensityStyles/Compact.xaml") });
        scroller.Children.Add(view);
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var primary = (Expander)FindInspectorElement(view, "PrimarySourceDisclosure");
        var secondary = (Expander)FindInspectorElement(view, "SecondarySourceDisclosure");
        _ = primary.IsExpanded.Should().BeFalse();
        _ = secondary.IsExpanded.Should().BeFalse();
        var collapsedHeader = primary.FindDescendant<ToggleButton>(toggle => toggle.Name == "ExpanderHeader")!;
        _ = primary.ActualHeight.Should().BeApproximately(collapsedHeader.ActualHeight, 1);
        var sourceCard = ((Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "AtmosphereLightsSection")).Items.OfType<Oxygen.Editor.Controls.PropertyCard>().Single();
        var separator = ((StackPanel)sourceCard.Content).Children.OfType<Grid>().Single().Children.OfType<Border>().Single();
        var separatorGap = separator.TransformToVisual(view).TransformPoint(default).Y
            - collapsedHeader.TransformToVisual(view).TransformPoint(default).Y - collapsedHeader.ActualHeight;
        _ = separatorGap.Should().BeApproximately(8, 1);
        _ = ((Button)view.FindName("ClearPropertySearchButton")).Visibility.Should().Be(Visibility.Collapsed);
        primary.IsExpanded = true;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var source = (AtmosphereSourceView)primary.Content;
        _ = source.ActualWidth.Should().BeGreaterThan(340);
        var azimuth = source.FindDescendant<NumberBox>(number => Equals(number.Tag, "SunAzimuth"))!;
        _ = azimuth.ActualWidth.Should().BeGreaterThan(140);
        _ = source.FindDescendant<TextBlock>(text => text.Text == "Azimuth")!.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
        _ = azimuth.LabelPosition.Should().Be(LabelPosition.Left);
        _ = azimuth.Label.Should().Be("Azimuth");
        var valueBorder = azimuth.FindDescendant<Border>(border => border.Name == "PartBackgroundBorder")!;
        _ = valueBorder.ActualWidth.Should().BeGreaterThanOrEqualTo(126);
        _ = (valueBorder.TransformToVisual(azimuth).TransformPoint(default).X - valueBorder.Margin.Left)
            .Should().BeApproximately(((azimuth.ActualWidth - 12) * 0.4) + 12, 1);
        var elevation = source.FindDescendant<NumberBox>(number => Equals(number.Tag, "SunElevation"))!;
        var rowPitch = elevation.TransformToVisual(source).TransformPoint(default).Y - azimuth.TransformToVisual(source).TransformPoint(default).Y;
        _ = rowPitch.Should().BeApproximately(azimuth.ActualHeight + 4, 1);
        foreach (var card in source.FindDescendants().OfType<Oxygen.Editor.Controls.PropertyCard>())
        {
            _ = card.Margin.Should().Be(new Thickness(0));
            _ = card.Padding.Should().Be(new Thickness(0));
        }
        var disclosureHeader = primary.FindDescendant<ToggleButton>(toggle => toggle.Name == "ExpanderHeader")!;
        var disclosureGap = source.TransformToVisual(primary).TransformPoint(default).Y
            - disclosureHeader.TransformToVisual(primary).TransformPoint(default).Y - disclosureHeader.ActualHeight;
        _ = disclosureGap.Should().BeApproximately(0, 1);
        var section = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "AtmosphereLightsSection");
        var references = section.FindDescendants().OfType<ComboBox>().ToArray();
        _ = references.Should().HaveCount(2);
        foreach (var reference in references)
        {
            var row = Grid.GetRow(reference);
            var rowActions = section.FindDescendants().OfType<Button>().Where(button => Grid.GetRow(button) == row && Grid.GetColumn(button) > 0).ToArray();
            _ = rowActions.Should().HaveCount(2);
            var referenceCenter = reference.TransformToVisual(section).TransformPoint(default).Y + (reference.ActualHeight / 2);
            _ = reference.ActualHeight.Should().BeGreaterThanOrEqualTo(32);
            _ = reference.FontSize.Should().Be(14);
            foreach (var action in rowActions)
            {
                var actionCenter = action.TransformToVisual(section).TransformPoint(default).Y + (action.ActualHeight / 2);
                _ = actionCenter.Should().BeApproximately(referenceCenter, 1);
                _ = action.ActualWidth.Should().BeGreaterThanOrEqualTo(32);
                _ = action.ActualHeight.Should().BeGreaterThanOrEqualTo(32);
                var icon = (FontIcon)action.Content;
                var iconCenter = icon.TransformToVisual(section).TransformPoint(default).Y + (icon.ActualHeight / 2);
                _ = iconCenter.Should().BeApproximately(referenceCenter, 1);
            }
        }
        var sectionResources = view.SectionView("AtmosphereLights").Resources;
        var headerToggles = section.FindDescendants().OfType<ToggleButton>().Where(toggle => ReferenceEquals(toggle.Style, sectionResources["QuietInspectorToggle"])).ToArray();
        _ = headerToggles.Should().NotBeEmpty();
        foreach (var toggle in headerToggles)
        {
            _ = ((Microsoft.UI.Xaml.Media.SolidColorBrush)toggle.Background).Color.A.Should().Be(0);
        }
        var selector = (CommunityToolkit.WinUI.Controls.Segmented)view.FindName("ScenePropertyScopeSelector");
        var scopes = selector.Items.OfType<CommunityToolkit.WinUI.Controls.SegmentedItem>().ToArray();
        _ = scopes.Max(item => item.ActualWidth).Should().BeApproximately(scopes.Min(item => item.ActualWidth), 1);
        _ = (scopes.Sum(item => item.ActualWidth) + 6).Should().BeLessThanOrEqualTo(selector.ActualWidth + 1);
        _ = scopes.Should().OnlyContain(item => item.ActualHeight <= 28 && item.FontSize == 11);
        var search = (TextBox)view.FindName("ScenePropertySearchBox");
        var beforeScroll = search.TransformToVisual(view).TransformPoint(default);
        var propertyScroll = (ScrollViewer)view.FindName("ScenePropertyScroll");
        _ = propertyScroll.ChangeView(null, 200, null, disableAnimation: true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = search.TransformToVisual(view).TransformPoint(default).Y.Should().Be(beforeScroll.Y);
        _ = propertyScroll.ChangeView(null, 0, null, disableAnimation: true);

        if (int.TryParse(Environment.GetEnvironmentVariable("OXYGEN_UI_CAPTURE_HOLD_SECONDS"), out var captureSeconds) && captureSeconds > 0)
        {
            await Task.Delay(TimeSpan.FromSeconds(captureSeconds), this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>The material editor uses its real XAML and a loaded material document.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task MaterialEditorXamlLoadsItsNumericControls() => this.InspectorXamlLoadsItsNumericControls("Material");

    /// <summary>Section headers have one full-width disclosure target and retain the required sky controls.</summary>
    /// <returns>The design-contract regression task.</returns>
    [TestMethod]
    public Task SceneHeaderAndSkyControlsFollowDesignContract() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model };
        var host = new Grid { Width = 420, Height = 780 };
        host.Children.Add(view);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var atmosphere = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "AtmosphereLightsSection");
        var header = atmosphere.FindDescendant<ToggleButton>(button => button.Name == "ExpanderHeader")!;
        var icon = header.FindDescendant<Viewbox>(element => element.Name == "PartHeaderIconPresenterHolder");
        _ = icon.Should().NotBeNull();
        var chevron = header.FindDescendant<AnimatedIcon>(element => element.Name == "ExpandCollapseChevron")!;
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
        var sky = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "SkyAtmosphereSection");
        var sunDisk = SceneCards(sky).Single(card => card.PropertyName == "Sun Disk");
        _ = sky.BringItemIntoView(sunDisk);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = sunDisk.FindDescendant<ToggleSwitch>(control => Equals(control.Tag, "SunDiskEnabled")).Should().NotBeNull();
        _ = SceneFieldLabels(sky).Should().Contain(["Distance scale", "Scattering strength", "Start distance", "Height fog contribution"]);
        var planet = sky.Items.OfType<Expander>().Single(group => Equals(group.Header, "Planet & ground"));
        planet.IsExpanded = true;
        _ = sky.BringItemIntoView(planet);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var albedoField = (Oxygen.Editor.World.Inspector.Controls.InspectorRgbField)await FindInspectorControlAsync(
            (ScrollViewer)view.FindName("ScenePropertyScroll"),
            () => view.FindDescendant<Oxygen.Editor.World.Inspector.Controls.InspectorRgbField>(field =>
                field.IsLoaded && Equals(field.Tag, "GroundAlbedo") && ((Oxygen.Editor.Controls.PropertyCard)field.Content).LeadingContent is Button),
            "GroundAlbedo", this.TestContext.CancellationToken).ConfigureAwait(true);
        var albedo = (Oxygen.Editor.Controls.PropertyCard)albedoField.Content;
        var swatch = (Button)albedo.LeadingContent!;
        _ = swatch.Flyout.Should().BeOfType<Flyout>();
        model.SkyAtmosphere.GroundAlbedoR = 0.21404114f;
        await model.PendingEdits.ConfigureAwait(true);
        _ = InspectorRgbPresentation.ToDisplayColor(model.SkyAtmosphere.GroundAlbedoColor).R.Should().Be(128);
        _ = ((Microsoft.UI.Xaml.Media.SolidColorBrush)((Border)swatch.Content).Background).Color.R.Should().Be(128);
        var historyCount = fixture.Context.History.UndoStack.Count;
        ((TextBox)view.FindName("ScenePropertySearchBox")).Text = "Ground Albedo";
        _ = sky.BringItemIntoView(planet);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        await PickDisplayColorAsync(swatch, Windows.UI.Color.FromArgb(255, 128, 64, 32)).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Scene.Environment.SkyAtmosphere.GroundAlbedoRgb.Y.Should().BeApproximately(0.05126946f, 0.000001f);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(historyCount + 1);
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = InspectorRgbPresentation.ToDisplayColor(model.SkyAtmosphere.GroundAlbedoColor).R.Should().Be(128);
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
        _ = number.FindDescendant<TextBlock>(text => text.Name == "PartValueTextBlock")!.Text.Should().Be(expected);
        _ = number.NumberValue.Should().Be(value);
        _ = number.Mask.Should().Be("~.###");
    });

    /// <summary>Compact display preserves units and nonnumeric placeholders.</summary>
    /// <param name="text">The existing mask-formatted text.</param>
    /// <param name="expected">The compact text.</param>
    [TestMethod]
    [DataRow("-.-", "-.-")]
    [DataRow("12.300 m", "12.3 m")]
    [DataRow(".536", "0.536")]
    public void SceneNumericDisplayPreservesUnitsAndPlaceholders(string text, string expected)
        => _ = new CompactNumberDisplayConverter().Convert(text, typeof(string), null!, "en-US").Should().Be(expected);

    /// <summary>Scene search reveals matching inactive exposure fields with their applicability state.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task ScenePropertySearchRevealsInactiveAutoFieldsAndScopeFiltersSections() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model };
        var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);

        var exposure = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "ExposureSection");
        var sky = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "SkyAtmosphereSection");
        var background = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "BackgroundSection");
        exposure.IsExpanded = false;
        var autoMinimum = (Oxygen.Editor.Controls.PropertyCard)((Oxygen.Editor.Controls.InspectorNumberField)FindInspectorElement(view, "AutoExposureMinEvCard")).Content;
        _ = model.Exposure.ExposureMode.Should().Be(Oxygen.Editor.World.Serialization.ExposureMode.Manual);
        _ = ((FrameworkElement)FindInspectorElement(view, "AutoExposureMinEvCard")).Visibility.Should().Be(Visibility.Collapsed);

        var search = view.FindDescendant<TextBox>(element => string.Equals(element.Name, "ScenePropertySearchBox", StringComparison.Ordinal))!;
        search.Text = "auto_exposure_min_ev";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

        _ = ((FrameworkElement)FindInspectorElement(view, "AutoExposureMinEvCard")).Visibility.Should().Be(Visibility.Visible);
        _ = ((StackPanel)autoMinimum.Content).Children.OfType<TextBlock>().Should().Contain(element => element.Text.Contains("applies in Auto exposure mode", StringComparison.Ordinal));
        _ = exposure.IsExpanded.Should().BeTrue();

        var scopeSelector = (CommunityToolkit.WinUI.Controls.Segmented)view.FindName("ScenePropertyScopeSelector");
        scopeSelector.SelectedIndex = 2;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = sky.Visibility.Should().Be(Visibility.Collapsed);
        _ = background.Visibility.Should().Be(Visibility.Collapsed);
        _ = exposure.Visibility.Should().Be(Visibility.Visible);

        search.Text = "manual";
        _ = ((FrameworkElement)FindInspectorElement(view, "AutoExposureMinEvCard")).Visibility.Should().Be(Visibility.Collapsed);
        search.Text = string.Empty;
        _ = exposure.IsExpanded.Should().BeFalse();
        _ = background.Visibility.Should().Be(Visibility.Collapsed);

        scopeSelector.SelectedIndex = 0;
        search.Text = "background";
        _ = background.Visibility.Should().Be(Visibility.Visible);
        _ = sky.Visibility.Should().Be(Visibility.Collapsed);
        _ = exposure.Visibility.Should().Be(Visibility.Collapsed);
        search.Text = "no-such-property";
        _ = background.Visibility.Should().Be(Visibility.Collapsed);
        _ = ((TextBlock)view.FindName("NoScenePropertyMatches")).Visibility.Should().Be(Visibility.Visible);
        search.Text = string.Empty;
        _ = background.Visibility.Should().Be(Visibility.Visible);

        model.PostProcessing.ToneMapping = Oxygen.Editor.World.Serialization.ToneMappingMode.None;
        await model.PendingEdits.ConfigureAwait(true);
        var toneMapping = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "ToneMappingSection");
        _ = ((FrameworkElement)FindInspectorElement(view, "DisplayGammaCard")).Visibility.Should().Be(Visibility.Visible);
        _ = ((Oxygen.Editor.Controls.InspectorNumberField)FindInspectorElement(view, "DisplayGammaCard")).Label.Should().Be("Display Gamma");
    });

    /// <summary>The seven design-order sections preserve all 42 scene cards and closed secondary groups.</summary>
    /// <returns>The UI regression task.</returns>
    [TestMethod]
    public Task SceneSectionsPreserveCardsAndRestoreNestedDisclosureAfterSearch() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model };
        var host = new Grid { Width = 420, Height = 780 };
        host.Children.Add(view);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var sections = ((StackPanel)view.FindName("SceneSections")).Children.SelectMany(child => child switch
        {
            UserControl { Content: Oxygen.Editor.Controls.PropertiesExpander section } => [section],
            UserControl { Content: StackPanel effects } => effects.Children.OfType<Oxygen.Editor.Controls.PropertiesExpander>(),
            _ => Enumerable.Empty<Oxygen.Editor.Controls.PropertiesExpander>(),
        }).ToArray();
        _ = sections.Select(section => section.Header).Should().Equal("Atmosphere Lights", "Sky Atmosphere", "Background", "Exposure", "Tone Mapping", "Color Grading", "Bloom");
        _ = sections.SelectMany(SceneCards).Should().HaveCount(42);
        _ = sections.Select(section => section.IsExpanded).Should().Equal(true, true, true, true, true, false, false);
        var sky = sections[1];
        var skyGroups = sky.Items.OfType<Expander>().ToArray();
        _ = skyGroups.Select(group => group.Header).Should().Equal("Planet & ground", "Scattering", "Aerial perspective");
        _ = skyGroups.Select(group => group.IsExpanded).Should().OnlyContain(expanded => !expanded);
        var planet = skyGroups[0];
        _ = planet.IsExpanded.Should().BeFalse();
        AssertDisclosureCardOrder(skyGroups[0], "PlanetRadiusKm", "AtmosphereHeightKm", "GroundAlbedo");
        AssertDisclosureCardOrder(skyGroups[1], "RayleighScaleHeightKm", "MieScaleHeightKm", "MieAnisotropy");
        AssertDisclosureCardOrder(skyGroups[2], "AerialPerspectiveDistanceScale", "AerialScatteringStrength", "AerialPerspectiveStartDepthMeters", "HeightFogContribution");

        var exposureGroups = sections[3].Items.OfType<Expander>().ToArray();
        _ = exposureGroups.Select(group => group.Header).Should().Equal("Metering & limits", "Adaptation", "Histogram & calibration", "Exposure shaping");
        _ = exposureGroups.Select(group => group.IsExpanded).Should().Equal(true, false, false, false);
        AssertDisclosureCardOrder(exposureGroups[0], "AutoExposureMeteringMode", "AutoExposureMinEv", "AutoExposureMaxEv", "AutoExposureTargetLuminance", "AutoExposureSpotMeterRadius");
        AssertDisclosureCardOrder(exposureGroups[1], "AutoExposureSpeedUp", "AutoExposureSpeedDown", "AutoExposureTransitionDistanceEv");
        AssertDisclosureCardOrder(exposureGroups[2], "ExposureKey", "AutoExposureLowPercentile", "AutoExposureHighPercentile", "AutoExposureMinLogLuminance", "AutoExposureLogLuminanceRange", "AutoExposureBlackInfluence");
        AssertDisclosureCardOrder(exposureGroups[3], "AutoExposureMeteringMask", "AutoExposureCompensationCurve");
        var disclosures = new[]
        {
            (Owner: sky, Item: skyGroups[0], AutomationId: "Scene.Sky.PlanetGround", Query: "PlanetRadiusKm"),
            (Owner: sky, Item: skyGroups[1], AutomationId: "Scene.Sky.Scattering", Query: "RayleighScaleHeightKm"),
            (Owner: sky, Item: skyGroups[2], AutomationId: "Scene.Sky.AerialPerspective", Query: "AerialPerspectiveDistanceScale"),
            (Owner: sections[3], Item: exposureGroups[0], AutomationId: "Scene.Exposure.MeteringLimits", Query: "AutoExposureMinEv"),
            (Owner: sections[3], Item: exposureGroups[1], AutomationId: "Scene.Exposure.Adaptation", Query: "AutoExposureSpeedUp"),
            (Owner: sections[3], Item: exposureGroups[2], AutomationId: "Scene.Exposure.HistogramCalibration", Query: "ExposureKey"),
            (Owner: sections[3], Item: exposureGroups[3], AutomationId: "Scene.Exposure.ExposureShaping", Query: "AutoExposureCompensationCurve"),
        };

        var search = (TextBox)view.FindName("ScenePropertySearchBox");
        var scroller = (ScrollViewer)view.FindName("ScenePropertyScroll");
        foreach (var (owner, item, automationId, query) in disclosures)
        {
            search.Text = query;
            await WaitForRenderAsync().ConfigureAwait(true);
            owner.StartBringIntoView();
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(owner.UpdateLayout).ConfigureAwait(true);
            _ = owner.BringItemIntoView(item).Should().BeTrue();
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(owner.UpdateLayout).ConfigureAwait(true);
            var group = (Expander)await FindInspectorControlAsync(scroller, () => owner.FindDescendant<Expander>(candidate =>
                candidate.ActualHeight > 0 && string.Equals(AutomationProperties.GetAutomationId(candidate), automationId, StringComparison.Ordinal)),
                automationId, this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = group.ApplyTemplate();
            var header = group.FindDescendant<ToggleButton>(toggle => string.Equals(toggle.Name, "ExpanderHeader", StringComparison.Ordinal))!;
            _ = header.ApplyTemplate();
            var headerContent = header.Content.Should().BeOfType<Grid>().Which;
            var chevron = headerContent.Children.OfType<FontIcon>().Single();
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
                _ = chevron.Glyph.Should().Be(expectedExpanded ? "\uE70D" : "\uE76C");
                _ = content.Visibility.Should().Be(expectedExpanded ? Visibility.Visible : Visibility.Collapsed);
            }

            if (group.IsExpanded != originallyExpanded)
            {
                Toggle(header);
                _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            }
        }

        search.Text = string.Empty;
        await WaitForRenderAsync().ConfigureAwait(true);
        search.Text = "ground_albedo";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = planet.IsExpanded.Should().BeTrue();
        _ = ((FrameworkElement)FindInspectorElement(view, "GroundAlbedoCard")).Visibility.Should().Be(Visibility.Visible);
        search.Text = string.Empty;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = planet.IsExpanded.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    private static IEnumerable<Oxygen.Editor.Controls.PropertyCard> SceneCards(Oxygen.Editor.Controls.PropertiesExpander section)
        => section.Items.SelectMany(item => item switch
        {
            Oxygen.Editor.Controls.PropertyCard card => [card],
            Oxygen.Editor.Controls.InspectorNumberField { Content: Oxygen.Editor.Controls.PropertyCard card } => [card],
            Oxygen.Editor.World.Inspector.Controls.InspectorRgbField { Content: Oxygen.Editor.Controls.PropertyCard card } => [card],
            Expander { Content: StackPanel content } => content.Children.SelectMany(child => child switch
            {
                Oxygen.Editor.Controls.PropertyCard card => [card],
                Oxygen.Editor.Controls.InspectorNumberField { Content: Oxygen.Editor.Controls.PropertyCard card } => [card],
                Oxygen.Editor.World.Inspector.Controls.InspectorRgbField { Content: Oxygen.Editor.Controls.PropertyCard card } => [card],
                _ => Enumerable.Empty<Oxygen.Editor.Controls.PropertyCard>(),
            }),
            _ => Enumerable.Empty<Oxygen.Editor.Controls.PropertyCard>(),
        });

    private static IEnumerable<string> SceneFieldLabels(Oxygen.Editor.Controls.PropertiesExpander section)
        => section.Items.SelectMany(item => item switch
        {
            Expander { Content: StackPanel content } => content.Children.Cast<object>(),
            _ => [item],
        }).Select(item => item switch
        {
            Oxygen.Editor.Controls.InspectorNumberField number => number.Label,
            Oxygen.Editor.World.Inspector.Controls.InspectorRgbField rgb => rgb.Label,
            Oxygen.Editor.Controls.PropertyCard card => card.PropertyName,
            _ => throw new InvalidOperationException($"Unexpected field composition {item.GetType().Name}."),
        });

    [TestMethod]
    public Task SceneSearchKeepsFieldContentAndStoredValuesWhileApplicabilityChanges() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model, Width = 420, Height = 780 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var exposure = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "ExposureSection");
        var spot = (Oxygen.Editor.Controls.PropertyCard)((Oxygen.Editor.Controls.InspectorNumberField)FindInspectorElement(view, "AutoExposureSpotMeterRadiusCard")).Content;
        var originalContent = spot.Content;
        var storedRadius = fixture.Scene.Environment.PostProcess.AutoExposureSpotMeterRadius;
        var search = (TextBox)view.FindName("ScenePropertySearchBox");
        search.Text = "spot radius";
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = ((FrameworkElement)FindInspectorElement(view, "AutoExposureSpotMeterRadiusCard")).Visibility.Should().Be(Visibility.Visible);
        _ = ((StackPanel)spot.Content).Children.OfType<TextBlock>().Should().Contain(note =>
            note.Text.Contains("applies in Auto exposure mode with Spot metering", StringComparison.Ordinal));
        _ = spot.Content.Should().BeSameAs(originalContent);
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();

        model.Exposure.ExposureMode = Oxygen.Editor.World.Serialization.ExposureMode.Auto;
        model.Exposure.AutoExposureMeteringMode = Oxygen.Editor.World.Serialization.MeteringMode.Spot;
        await model.PendingEdits.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = ((StackPanel)spot.Content).Children.OfType<TextBlock>().Should().NotContain(note =>
            note.Visibility == Visibility.Visible && note.Text.StartsWith("Stored value", StringComparison.Ordinal));
        search.Text = "spot radius missing-field";
        _ = ((TextBlock)view.FindName("NoScenePropertyMatches")).Visibility.Should().Be(Visibility.Visible);
        search.Text = string.Empty;
        _ = spot.Content.Should().BeSameAs(originalContent);
        _ = fixture.Scene.Environment.PostProcess.AutoExposureSpotMeterRadius.Should().Be(storedRadius);
    });

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
        secondModel.Exposure.ExposureMode = Oxygen.Editor.World.Serialization.ExposureMode.Auto;
        await secondModel.PendingEdits.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var exposure = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "ExposureSection");
        var minimum = (Oxygen.Editor.Controls.PropertyCard)((Oxygen.Editor.Controls.InspectorNumberField)FindInspectorElement(view, "AutoExposureMinEvCard")).Content;
        _ = ((StackPanel)minimum.Content).Children.OfType<TextBlock>().Should().NotContain(note =>
            note.Visibility == Visibility.Visible && note.Text.StartsWith("Stored value", StringComparison.Ordinal));
        firstModel.Exposure.ExposureMode = Oxygen.Editor.World.Serialization.ExposureMode.Auto;
        await firstModel.PendingEdits.ConfigureAwait(true);
        _ = view.ViewModel.Should().BeSameAs(secondModel);
        _ = second.Scene.Environment.PostProcess.AutoExposureMinEv.Should().Be(-6);
    });

    private static void AssertDisclosureCardOrder(Expander disclosure, params string[] propertyIdentities)
    {
        var identities = ((StackPanel)disclosure.Content).Children.Select(child => child switch
        {
            Oxygen.Editor.Controls.InspectorNumberField numeric => (string)numeric.Tag,
            Oxygen.Editor.World.Inspector.Controls.InspectorRgbField rgb => (string)rgb.Tag,
            Oxygen.Editor.Controls.PropertyCard card => GetCardPropertyIdentity(card),
            _ => throw new InvalidOperationException($"Unexpected field composition {child.GetType().Name}."),
        }).ToArray();
        _ = identities.Should().Equal(propertyIdentities);
    }

    private static string GetCardPropertyIdentity(Oxygen.Editor.Controls.PropertyCard card)
    {
        if (card.Content is FrameworkElement { Tag: string directTag })
        {
            return directTag.Split(' ', StringSplitOptions.RemoveEmptyEntries)[0];
        }

        if (card.Content is DependencyObject content
            && content.FindDescendant<FrameworkElement>(element => element.Tag is string) is { Tag: string nestedTag })
        {
            return nestedTag.Split(' ', StringSplitOptions.RemoveEmptyEntries)[0];
        }

        throw new InvalidOperationException($"Card '{card.PropertyName}' has no stable tagged editor identity.");
    }

    /// <summary>The realized near-plane editor rejects an invalid commit and displays current inline feedback.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task CameraTextCommitRejectsInvalidNearPlaneAndClearsItsDiagnosticOnCorrection() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (PerspectiveCameraViewModel)CreateModel("Camera", fixture);
        var view = new PerspectiveCameraView
        {
            ViewModel = model
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, "NearPlane"))!;
        var before = fixture.Camera.NearPlane;
        await EnterTextAsync(number, "2000").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Camera.NearPlane.Should().Be(before);
        _ = number.NumberValue.Should().Be(before);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = model.NearPlaneDiagnostic.Message.Should().NotBeEmpty();
        _ = view.FindDescendants().OfType<TextBlock>().Should().Contain(element => string.Equals(element.Text, model.NearPlaneDiagnostic.Message, StringComparison.Ordinal) && element.Visibility == Visibility.Visible);
        await EnterTextAsync(number, "2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Camera.NearPlane.Should().Be(2);
        _ = model.NearPlaneDiagnostic.Message.Should().BeEmpty();
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
            ViewModel = model
        };
        var scroller = new ScrollViewer
        {
            Content = view,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto
        };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var card = view.FindDescendant<Oxygen.Editor.Controls.PropertyCard>(element => string.Equals(element.PropertyName, "Scale", StringComparison.Ordinal))!;
        card.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false });
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        var number = card.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        await EnterTextAsync(number, "0").ConfigureAwait(true);
        _ = fixture.Node.Components.OfType<TransformComponent>().Single().LocalScale.X.Should().Be(1);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = model.ScaleXDiagnostic.Message.Should().NotBeEmpty();
        _ = view.FindDescendants().OfType<TextBlock>().Should().Contain(element => string.Equals(element.Text, model.ScaleXDiagnostic.Message, StringComparison.Ordinal) && element.Visibility == Visibility.Visible);
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
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView
        {
            ViewModel = model
        };
        var host = new Grid
        {
            Width = 420,
            Height = 780,
        };
        host.Children.Add(view);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        if (field == "GroundAlbedo")
        {
            var sky = (Oxygen.Editor.Controls.PropertiesExpander)FindInspectorElement(view, "SkyAtmosphereSection");
            sky.Items.OfType<Expander>().Single(group => Equals(group.Header, "Planet & ground")).IsExpanded = true;
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }

        var propertyScroll = (ScrollViewer)view.FindName("ScenePropertyScroll");
        var vector = await FindVisibleVectorAsync(view, propertyScroll, field).ConfigureAwait(true);
        var number = vector.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        var before = number.NumberValue;
        RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
        for (var sample = 1; sample <= 100; sample++)
        {
            number.NumberValue = sample / 200f;
        }

        RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = number.NumberValue.Should().Be(0.5f);
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before);
    });
}
