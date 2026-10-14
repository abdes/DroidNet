// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace DroidNet.Controls.Tests;

/// <summary>Verifies label emphasis never alters numeric-editor layout or channel brushes.</summary>
[TestClass]
[TestCategory("UITest")]
public sealed class NumberBoxLabelPresentationTests : VisualUserInterfaceTests
{
    private static readonly string[] VisualStates = ["Hover", "Pressed", "Normal", "Hover", "Normal"];
    private static readonly double[] ProportionalWidths = [480d, 760d, 340d, 480d];

    [TestMethod]
    [DataRow(340d, 14d, false)]
    [DataRow(260d, 14d, false)]
    [DataRow(480d, 28d, false)]
    [DataRow(120d, 14d, true)]
    [DataRow(120d, 28d, true)]
    public Task HoverAndPressPreserveFontMetricsGeometryAndForeground(double width, double fontSize, bool compact) => EnqueueAsync(async () =>
    {
        var brush = new SolidColorBrush(Colors.OrangeRed);
        var number = new NumberBox
        {
            Width = width,
            FontSize = fontSize,
            IsCompact = compact,
            Label = compact ? "R" : "Atmosphere height",
            LabelPosition = LabelPosition.Left,
            LabelForeground = brush,
            LabelWidth = compact ? double.NaN : 124,
            AutoStackLabel = !compact,
        };
        await LoadTestContentAsync(number).ConfigureAwait(true);
        _ = VisualStateManager.GoToState(number, "Normal", useTransitions: false).Should().BeTrue();
        number.UpdateLayout();
        var partName = compact ? "PartCompactLabelTextBlock" : "PartLabelTextBlock";
        var label = number.FindDescendant<TextBlock>(part => string.Equals(part.Name, partName, StringComparison.Ordinal))!;
        var field = number.FindDescendant<Border>(part => string.Equals(part.Name, "PartBackgroundBorder", StringComparison.Ordinal))!;
        var labelBounds = Bounds(label, number);
        var fieldBounds = Bounds(field, number);
        var desiredSize = number.DesiredSize;
        var weight = label.FontWeight;
        var labelFontSize = label.FontSize;
        foreach (var state in VisualStates)
        {
            _ = VisualStateManager.GoToState(number, state, useTransitions: false).Should().BeTrue();
            number.UpdateLayout();
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            _ = label.FontWeight.Should().Be(weight);
            _ = label.FontSize.Should().Be(labelFontSize);
            _ = Bounds(label, number).Should().Be(labelBounds);
            _ = Bounds(field, number).Should().Be(fieldBounds);
            _ = number.DesiredSize.Should().Be(desiredSize);
            _ = label.Foreground.Should().BeSameAs(brush);
            _ = brush.Color.Should().Be(Colors.OrangeRed);
            _ = brush.Opacity.Should().Be(1);
            _ = label.Opacity.Should().BeApproximately(string.Equals(state, "Normal", StringComparison.Ordinal) ? 0.85 : 1, 0.000001);
        }
    });

    [TestMethod]
    [DataRow(LabelPosition.Left)]
    [DataRow(LabelPosition.Right)]
    public Task ProportionalLabelsReserveTheSameValueShareIncludingAnnotations(LabelPosition position) => EnqueueAsync(async () =>
    {
        var number = new NumberBox
        {
            Width = 480,
            Label = "A long numeric property caption that needs an ellipsis",
            LabelPosition = position,
            LabelWidth = 124,
            LabelWidthRatio = 0.4,
            LabelSpacing = 12,
            Prefix = "f/",
            Unit = "EV",
            AutoStackLabel = true,
        };
        await LoadTestContentAsync(number).ConfigureAwait(true);
        var label = number.FindDescendant<TextBlock>(part => string.Equals(part.Name, "PartLabelTextBlock", StringComparison.Ordinal))!;
        var valueGroup = number.FindDescendant<Grid>(part => string.Equals(part.Name, "PartValueGroup", StringComparison.Ordinal))!;
        foreach (var width in ProportionalWidths)
        {
            number.Width = width;
            number.UpdateLayout();
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            _ = number.ActualLabelPosition.Should().Be(position);
            _ = valueGroup.ActualWidth.Should().BeApproximately((width - 12) * 0.6, 1);
            _ = label.ActualWidth.Should().BeLessThanOrEqualTo(((width - 12) * 0.4) + 1);
            _ = label.TextWrapping.Should().Be(TextWrapping.NoWrap);
            _ = label.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
            _ = label.IsTextTrimmed.Should().BeTrue();
        }

        number.Width = 260;
        number.UpdateLayout();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = number.ActualLabelPosition.Should().Be(LabelPosition.Top);
        number.LabelWidthRatio = double.NaN;
        number.UpdateLayout();
        _ = label.TextWrapping.Should().Be(TextWrapping.Wrap);
        _ = label.TextTrimming.Should().Be(TextTrimming.None);
    });

    [TestMethod]
    public Task UnitSitsOutsideTheValueFieldAndReportsItsWidth() => EnqueueAsync(async () =>
    {
        var number = new NumberBox { Mask = "~.##", NumberValue = 1.5, Unit = "lux" };
        await LoadTestContentAsync(number).ConfigureAwait(true);
        var unit = number.FindDescendant<TextBlock>(part => string.Equals(part.Name, "PartValueUnit", StringComparison.Ordinal))!;
        var field = number.FindDescendant<Border>(part => string.Equals(part.Name, "PartBackgroundBorder", StringComparison.Ordinal))!;

        _ = unit.Text.Should().Be("lux");
        _ = Bounds(unit, number).Left.Should().BeGreaterThanOrEqualTo(Bounds(field, number).Right, "the unit is not part of the edited value");
        _ = number.UnitWidth.Should().BeApproximately(unit.ActualWidth + unit.Margin.Left, 0.5, "the width includes the unit's gap");
        _ = number.DisplayText.Should().NotContain("lux");

        number.Unit = string.Empty;
        number.UpdateLayout();

        _ = unit.Visibility.Should().Be(Visibility.Collapsed);
        _ = number.UnitWidth.Should().Be(0);
    });

    private static Rect Bounds(FrameworkElement element, UIElement relativeTo)
        => new(element.TransformToVisual(relativeTo).TransformPoint(default), element.RenderSize);
}
