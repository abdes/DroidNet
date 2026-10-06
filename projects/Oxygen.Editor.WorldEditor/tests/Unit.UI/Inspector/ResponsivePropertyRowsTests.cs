// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Controls;
using Expander = Microsoft.UI.Xaml.Controls.Expander;
using InspectorRgbPresentation = Oxygen.Editor.World.Inspector.InspectorRgbPresentation;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

/// <summary>Exercises the owning property layout policies using real WinUI controls.</summary>
[TestClass]
internal sealed class ResponsivePropertyRowsTests : VisualUserInterfaceTests
{
    private static readonly string[] PropertyNames = ["Azimuth", "Elevation", "Angular diameter"];
    private static readonly double[] Widths = [760d, 620d, 900d, 760d];

    [TestMethod]
    [DataRow(260d, 1d)]
    [DataRow(340d, 1d)]
    [DataRow(480d, 1d)]
    [DataRow(260d, 1.5d)]
    [DataRow(420d, 1.5d)]
    [DataRow(480d, 2d)]
    [DataRow(760d, 2d)]
    public Task ScalarRowsUseSharedColumnsAndKeepUnitsWithValues(double width, double textScale) => EnqueueAsync(async () =>
    {
        var rows = new StackPanel { Spacing = 4 };
        foreach (var name in PropertyNames)
        {
            rows.Children.Add(new PropertyCard
            {
                PropertyName = name,
                Qualifier = "°",
                FontSize = 14 * textScale,
                Content = new NumberBox { NumberValue = 26.9f, FontSize = 14 * textScale, Mask = "~.###" },
            });
        }

        var host = CreateHost(width, rows);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        double? inputStart = null;
        double? suffixStart = null;
        foreach (var card in rows.Children.OfType<PropertyCard>())
        {
            var number = (NumberBox)card.Content;
            var label = number.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PartLabelTextBlock", StringComparison.Ordinal))!;
            var suffix = number.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PartValueQualifier", StringComparison.Ordinal))!;
            var field = number.FindDescendant<Border>(border => string.Equals(border.Name, "PartBackgroundBorder", StringComparison.Ordinal))!;
            var numberPoint = field.TransformToVisual(card).TransformPoint(default);
            var suffixPoint = suffix.TransformToVisual(card).TransformPoint(default);
            var expected = width >= 12 + Math.Max(124 * textScale / 0.4, ((128 * textScale) + 28) / 0.6)
                ? PropertyLayout.Inline : PropertyLayout.Stacked;
            _ = card.ActualLayout.Should().Be(expected);
            _ = label.TextWrapping.Should().Be(TextWrapping.NoWrap);
            _ = label.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
            _ = label.Text.Should().Be(card.PropertyName);
            _ = label.Visibility.Should().Be(Visibility.Visible);
            _ = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PropertyName", StringComparison.Ordinal))!.ActualWidth.Should().Be(0);
            _ = (field.ActualWidth + field.Margin.Left + field.Margin.Right).Should().BeGreaterThanOrEqualTo((128 * textScale) - 1);
            _ = (suffixPoint.X + suffix.ActualWidth).Should().BeLessThanOrEqualTo(width + 1);
            _ = (suffixPoint.X - numberPoint.X - field.ActualWidth - field.Margin.Right).Should().BeApproximately(4, 1);
            _ = suffixPoint.Y.Should().BeGreaterThanOrEqualTo(numberPoint.Y - 1);
            if (expected == PropertyLayout.Stacked)
            {
                _ = numberPoint.Y.Should().BeGreaterThanOrEqualTo(label.ActualHeight + 3);
            }
            else
            {
                _ = (numberPoint.X - field.Margin.Left).Should().BeApproximately(((width - 12) * 0.4) + 12, 1);
            }

            _ = numberPoint.X.Should().BeApproximately(inputStart ?? numberPoint.X, 1);
            _ = suffixPoint.X.Should().BeApproximately(suffixStart ?? suffixPoint.X, 1);
            inputStart = numberPoint.X;
            suffixStart = suffixPoint.X;
        }
    });

    [TestMethod]
    public Task MixedInlineRowsKeepTheSameProportionsAndEllipsisWhileResizing() => EnqueueAsync(async () =>
    {
        const string longLabel = "A very long inspector property label that must stay on a single line";
        var number = new NumberBox { NumberValue = 26.9f, Mask = "~.###" };
        var scalar = new PropertyCard { PropertyName = longLabel, Qualifier = "°", Content = number };
        var toggle = new PropertyCard { PropertyName = "Enabled", Content = new ToggleSwitch() };
        var compound = new PropertyCard
        {
            PropertyName = longLabel,
            IsCompound = true,
            Qualifier = "m",
            Content = new VectorBox { ComponentLabelPosition = LabelPosition.Left, ComponentMask = "~.###" },
        };
        var rows = new StackPanel { Spacing = 4 };
        rows.Children.Add(scalar);
        rows.Children.Add(toggle);
        rows.Children.Add(compound);
        var host = CreateHost(760, rows);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var scalarLabel = number.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PartLabelTextBlock", StringComparison.Ordinal))!;
        var scalarValue = number.FindDescendant<Grid>(grid => string.Equals(grid.Name, "PartValueGroup", StringComparison.Ordinal))!;

        foreach (var width in Widths)
        {
            host.Width = width;
            host.UpdateLayout();
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            var labelWidth = (width - 12) * 0.4;
            var valueWidth = (width - 12) * 0.6;
            var valueStart = scalarValue.TransformToVisual(rows).TransformPoint(default).X;
            _ = scalar.ActualLayout.Should().Be(PropertyLayout.Inline);
            _ = scalarValue.ActualWidth.Should().BeApproximately(valueWidth, 1);
            _ = valueStart.Should().BeApproximately(labelWidth + 12, 1);
            _ = scalarLabel.TextWrapping.Should().Be(TextWrapping.NoWrap);
            _ = scalarLabel.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
            _ = scalarLabel.IsTextTrimmed.Should().BeTrue();
            _ = ToolTipService.GetToolTip(scalarLabel).Should().Be(longLabel);

            foreach (var card in new[] { toggle, compound })
            {
                var label = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PropertyName", StringComparison.Ordinal))!;
                var presenter = card.FindDescendant<ContentPresenter>(part => string.Equals(part.Name, "PropertyEditor", StringComparison.Ordinal))!;
                var valueGroup = (Grid)presenter.Parent;
                _ = card.ActualLayout.Should().Be(PropertyLayout.Inline);
                _ = valueGroup.TransformToVisual(rows).TransformPoint(default).X.Should().BeApproximately(valueStart, 1);
                _ = valueGroup.ActualWidth.Should().BeApproximately(valueWidth, 1);
                _ = label.TextWrapping.Should().Be(TextWrapping.NoWrap);
                _ = label.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
                if (ReferenceEquals(card, compound))
                {
                    _ = label.IsTextTrimmed.Should().BeTrue();
                }
            }
        }
    });

    [TestMethod]
    [DataRow(480d, 1d)]
    [DataRow(260d, 1d)]
    [DataRow(480d, 2d)]
    public Task NativeScalarCaptionKeepsItsGestureWhenTheRowReflows(double width, double textScale) => EnqueueAsync(async () =>
    {
        var number = new NumberBox { NumberValue = 2, Mask = "~.###" };
        var card = new PropertyCard { PropertyName = "Angular diameter", Qualifier = "°", FontSize = 14 * textScale, Content = number };
        var host = CreateHost(width, card);
        host.HorizontalAlignment = HorizontalAlignment.Left;
        var viewport = new Grid { Width = 760, Height = 480 };
        viewport.Children.Add(host);
        using var scaledHost = new ScaledXamlHost();
        await scaledHost.LoadAsync(viewport, 1, CancellationToken.None).ConfigureAwait(true);
        var label = number.FindDescendant<TextBlock>(part => string.Equals(part.Name, "PartLabelTextBlock", StringComparison.Ordinal))!;
        var starts = 0;
        var completions = new List<NumberBoxEditCompletionKind?>();
        number.EditSessionStarted += (_, _) => starts++;
        number.EditSessionCompleted += (_, args) => completions.Add(args.CompletionKind);
        using var pointer = await NativePointer.PressAsync(label, CancellationToken.None).ConfigureAwait(true);
        await pointer.MoveAsync(12, CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().BeGreaterThan(2);
        var beforeResize = number.NumberValue;
        host.Width = width == 260 ? 760 : 260;
        host.UpdateLayout();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = number.FindDescendant<TextBlock>(part => string.Equals(part.Name, "PartLabelTextBlock", StringComparison.Ordinal)).Should().BeSameAs(label);
        await pointer.MoveAsync(12, CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().BeGreaterThan(beforeResize);
        await NativePointer.ReleaseAsync(CancellationToken.None).ConfigureAwait(true);
        _ = starts.Should().Be(1);
        _ = completions.Should().Equal(NumberBoxEditCompletionKind.Commit);

        host.Children.Clear();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        host.Children.Add(card);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(host.UpdateLayout).ConfigureAwait(true);
        var reloadedLabel = number.FindDescendant<TextBlock>(part => string.Equals(part.Name, "PartLabelTextBlock", StringComparison.Ordinal))!;
        using var reloadedPointer = await NativePointer.PressAsync(reloadedLabel, CancellationToken.None).ConfigureAwait(true);
        await reloadedPointer.MoveAsync(12, CancellationToken.None).ConfigureAwait(true);
        await NativePointer.ReleaseAsync(CancellationToken.None).ConfigureAwait(true);
        _ = starts.Should().Be(2);
        _ = completions.Should().Equal(NumberBoxEditCompletionKind.Commit, NumberBoxEditCompletionKind.Commit);
    });

    [TestMethod]
    [DataRow("position", 1d)]
    [DataRow("position", 1.5d)]
    [DataRow("position", 2d)]
    [DataRow("color", 1d)]
    [DataRow("color", 1.5d)]
    [DataRow("color", 2d)]
    [DataRow("multiplier", 1d)]
    [DataRow("multiplier", 1.5d)]
    [DataRow("multiplier", 2d)]
    public Task CompoundEditorsAdaptWithoutLosingQualifiersOrChannelOrder(string kind, double textScale) => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { ComponentLabelPosition = LabelPosition.Left, ComponentMask = "~.###", FontSize = 14 * textScale };
        if (!string.Equals(kind, "position", StringComparison.Ordinal))
        {
            InspectorRgbPresentation.Configure(vector);
        }

        AutomationProperties.SetName(vector, string.Equals(kind, "position", StringComparison.Ordinal) ? "Position, metres" : $"{kind}, RGB");
        var card = new PropertyCard
        {
            PropertyName = string.Equals(kind, "position", StringComparison.Ordinal) ? "Position" : string.Equals(kind, "color", StringComparison.Ordinal) ? "Color" : "Disk luminance scale",
            IsCompound = true,
            Qualifier = string.Equals(kind, "position", StringComparison.Ordinal) ? "m" : string.Equals(kind, "color", StringComparison.Ordinal) ? "Linear RGB" : "RGB ×",
            EditorMinimumWidth = 248,
            Layout = string.Equals(kind, "position", StringComparison.Ordinal) ? PropertyLayout.Auto : PropertyLayout.Stacked,
            FontSize = 14 * textScale,
            Content = vector,
            LeadingContent = string.Equals(kind, "color", StringComparison.Ordinal) ? new Button { MinWidth = 28, MinHeight = 32 } : null,
        };
        var host = CreateHost(760, card);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var inputs = vector.FindDescendants().OfType<NumberBox>().ToArray();
        foreach (var input in inputs)
        {
            input.FontSize = 14 * textScale;
        }

        foreach (var width in new[] { 760d, 480d, 420d, 340d, 280d, 760d })
        {
            host.Width = width;
            host.UpdateLayout();
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            var headerQualifier = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "HeaderQualifier", StringComparison.Ordinal))!;
            var valueQualifier = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "ValueQualifier", StringComparison.Ordinal))!;
            _ = headerQualifier.Visibility.Should().Be(card.ActualLayout == PropertyLayout.Stacked ? Visibility.Visible : Visibility.Collapsed);
            _ = valueQualifier.Visibility.Should().Be(card.ActualLayout == PropertyLayout.Inline ? Visibility.Visible : Visibility.Collapsed);
            _ = inputs.Should().HaveCount(3);
            _ = inputs.Select(input => input.Label).Should().Equal(string.Equals(kind, "position", StringComparison.Ordinal) ? ["X", "Y", "Z"] : ["R", "G", "B"]);
            _ = vector.FindDescendants().OfType<NumberBox>().Should().Equal(inputs);
            var points = inputs.Select(input => input.TransformToVisual(card).TransformPoint(default)).ToArray();
            for (var i = 0; i < inputs.Length; i++)
            {
                _ = AutomationProperties.GetName(inputs[i]).Should().Contain(string.Equals(kind, "position", StringComparison.Ordinal) ? "metres" : "RGB");
                _ = inputs[i].ActualWidth.Should().BeGreaterThanOrEqualTo(80 * textScale - 1,
                    $"dock width is {width}, vector width is {vector.ActualWidth}, padding is {vector.Padding}, vertical channels are {vector.AreComponentsStacked}");
                _ = (points[i].X + inputs[i].ActualWidth).Should().BeLessThanOrEqualTo(width + 1);
                _ = inputs[i].ActualWidth.Should().BeApproximately(inputs[0].ActualWidth, 1);
                if (i > 0)
                {
                    if (vector.AreComponentsStacked)
                    {
                        _ = points[i].X.Should().BeApproximately(points[0].X, 1);
                        _ = points[i].Y.Should().BeGreaterThan(points[i - 1].Y);
                    }
                    else
                    {
                        _ = points[i].Y.Should().BeApproximately(points[0].Y, 1);
                        _ = points[i].X.Should().BeGreaterThan(points[i - 1].X);
                    }
                }
            }

            if (width == 280 && textScale > 1)
            {
                _ = vector.AreComponentsStacked.Should().BeTrue();
            }

            if (width == 760 && textScale == 1 && string.Equals(kind, "position", StringComparison.Ordinal))
            {
                _ = card.ActualLayout.Should().Be(PropertyLayout.Inline);
            }

            if (width is 280 or 480 && textScale is 1 or 2)
            {
                await InspectorCapture.SaveIfRequestedAsync(card, $"{kind}-{width}-{textScale * 100}").ConfigureAwait(true);
            }
        }
    });

    [TestMethod]
    public Task HeaderActionsStaySeparateFromDisclosureAndWrapWithTheHeader() => EnqueueAsync(async () =>
    {
        var reset = new Button { Content = "Reset", MinHeight = 32 };
        var section = new PropertiesExpander
        {
            Header = "Sky Atmosphere",
            Description = "A supporting description that wraps without overlapping section actions.",
            HeaderActions = reset,
            IsExpanded = true,
        };
        section.Items.Add(new PropertyCard { PropertyName = "Elevation", Qualifier = "°", Content = new NumberBox() });
        var host = CreateHost(280, section);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        _ = reset.ActualWidth.Should().BeGreaterThan(0);
        _ = section.IsTabStop.Should().BeFalse();
        _ = section.FindDescendant<Expander>().Should().NotBeNull();
        _ = section.FindDescendants().OfType<Button>().Should().Contain(reset);
        foreach (var width in new[] { 480d, 280d })
        {
            host.Width = width;
            host.UpdateLayout();
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            var description = section.FindDescendant<ContentPresenter>(item => string.Equals(item.Name, "PartDescriptionPresenter", StringComparison.Ordinal))!;
            var descriptionPoint = description.TransformToVisual(section).TransformPoint(default);
            var actionPoint = reset.TransformToVisual(section).TransformPoint(default);
            _ = (descriptionPoint.X + description.ActualWidth).Should().BeLessThanOrEqualTo(actionPoint.X + 1);
            _ = (actionPoint.X + reset.ActualWidth).Should().BeLessThanOrEqualTo(section.ActualWidth + 1);
            _ = section.IsExpanded.Should().BeTrue();
        }
    });

    [TestMethod]
    [DataRow(ElementTheme.Light)]
    [DataRow(ElementTheme.Dark)]
    public Task DisclosureExpansionKeepsNeutralFeedbackAndKeyboardFocus(ElementTheme theme) => EnqueueAsync(async () =>
    {
        var expander = new Expander { Header = "Planet & ground", Content = new TextBlock { Text = "Planet Radius" } };
        var host = CreateHost(340, expander);
        host.RequestedTheme = theme;
        expander.Style = (Style)host.Resources["QuietDisclosure"];
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var header = expander.FindDescendant<ToggleButton>(button => string.Equals(button.Name, "ExpanderHeader", StringComparison.Ordinal))!;
        var presenter = header.FindDescendant<ContentPresenter>(item => string.Equals(item.Name, "ContentPresenter", StringComparison.Ordinal))!;
        var chevron = header.FindDescendant<FontIcon>(item => string.Equals(item.Name, "DisclosureChevron", StringComparison.Ordinal))!;
        _ = header.IsTabStop.Should().BeTrue();
        _ = header.UseSystemFocusVisuals.Should().BeTrue();
        _ = header.Focus(FocusState.Keyboard).Should().BeTrue();
        _ = chevron.Glyph.Should().Be("\uE76C");
        expander.IsExpanded = true;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = chevron.Glyph.Should().Be("\uE70D");
        _ = ((SolidColorBrush)presenter.Background).Color.A.Should().Be(0);

        foreach (var (closed, opened) in new[]
        {
            ("Normal", "Checked"),
            ("PointerOver", "CheckedPointerOver"),
            ("Pressed", "CheckedPressed"),
            ("Disabled", "CheckedDisabled"),
        })
        {
            _ = VisualStateManager.GoToState(header, closed, useTransitions: false).Should().BeTrue();
            var background = ((SolidColorBrush)presenter.Background).Color;
            var foreground = ((SolidColorBrush)presenter.Foreground).Color;
            _ = VisualStateManager.GoToState(header, opened, useTransitions: false).Should().BeTrue();
            _ = ((SolidColorBrush)presenter.Background).Color.Should().Be(background, $"expansion must not change {closed} feedback");
            _ = ((SolidColorBrush)presenter.Foreground).Color.Should().Be(foreground, $"expansion must not change {closed} text contrast");
        }

        _ = VisualStateManager.GoToState(header, "Checked", useTransitions: false);
        await InspectorCapture.SaveIfRequestedAsync(host, $"disclosure-open-{theme}").ConfigureAwait(true);
        for (var cycle = 0; cycle < 3; cycle++)
        {
            header.IsChecked = false;
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            _ = expander.IsExpanded.Should().BeFalse();
            _ = chevron.Glyph.Should().Be("\uE76C", "collapsed disclosures must point right after expansion");
            header.IsChecked = true;
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            _ = expander.IsExpanded.Should().BeTrue();
            _ = chevron.Glyph.Should().Be("\uE70D");
        }
    });

    [TestMethod]
    public Task ScalarMetadataUpdatesAndDetachmentRestoreTheEditorsOwnBindings() => EnqueueAsync(async () =>
    {
        var originalCaption = new TextBlock { Text = "Original caption" };
        var original = new NumberBox { LabelPosition = LabelPosition.Right, FontSize = 16, Prefix = "original" };
        original.SetBinding(NumberBox.LabelProperty, new Microsoft.UI.Xaml.Data.Binding
        {
            Source = originalCaption,
            Path = new PropertyPath(nameof(TextBlock.Text)),
            Mode = Microsoft.UI.Xaml.Data.BindingMode.OneWay,
        });
        var card = new PropertyCard { PropertyName = "Exposure", Prefix = "f/", Qualifier = "EV", Content = original };
        await LoadTestContentAsync(CreateHost(480, card)).ConfigureAwait(true);
        _ = original.Label.Should().Be("Exposure");
        _ = original.Prefix.Should().Be("f/");
        card.PropertyName = "Updated exposure";
        card.Qualifier = "ms";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = original.Label.Should().Be("Updated exposure");
        _ = original.Qualifier.Should().Be("ms");

        var replacement = new NumberBox { Label = "Replacement", LabelPosition = LabelPosition.Bottom };
        card.Content = replacement;
        _ = original.Label.Should().Be("Original caption");
        _ = original.LabelPosition.Should().Be(LabelPosition.Right);
        _ = original.FontSize.Should().Be(16);
        _ = original.Prefix.Should().Be("original");
        _ = original.LabelWidthRatio.Should().Be(double.NaN);
        originalCaption.Text = "Rebound caption";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = original.Label.Should().Be("Rebound caption");
        _ = replacement.Label.Should().Be("Updated exposure");
        card.UseEditorLabel = false;
        _ = replacement.Label.Should().Be("Replacement");
        _ = replacement.LabelPosition.Should().Be(LabelPosition.Bottom);
    });

    private static Grid CreateHost(double width, UIElement content)
    {
        var host = new Grid { Width = width };
        host.Resources.MergedDictionaries.Add(new ResourceDictionary
        {
            Source = new Uri("ms-appx:///Oxygen.Editor.WorldEditor/Inspector/SceneInspectorStyles.xaml"),
        });
        host.Children.Add(content);
        return host;
    }
}
