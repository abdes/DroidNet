// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using NumberBox = DroidNet.Controls.NumberBox;
using static Oxygen.Editor.Controls.Testing.PropertyEditorProbes;
using static Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors.LayoutHosts;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors;

/// <summary>
///     The responsive layout of the number and color fields: where the name, unit, qualifier and channels sit in the
///     row, and where feedback is placed.
/// </summary>
[TestClass]
internal sealed class FieldLayoutTests : VisualUserInterfaceTests
{
    private const string LongError = "This value is outside the range the engine accepts; enter a smaller value or reset it.";

    [TestMethod]
    [DataRow(260d, 1d)]
    [DataRow(340d, 1d)]
    [DataRow(480d, 1d)]
    [DataRow(420d, 1.5d)]
    [DataRow(480d, 2d)]
    [DataRow(760d, 2d)]
    public Task NumberFieldRowsShowTheNameInTheRowAndTheUnitAfterTheValue(double width, double textScale) => EnqueueAsync(async () =>
    {
        var host = CreateHost(width);
        var field = CreateNumberField("Near plane", "m");
        field.FontSize = 14 * textScale;
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var number = field.FindDescendant<NumberBox>()!;
        var card = CardOf(field);
        var label = card.FindDescendant<TextBlock>(part => string.Equals(part.Name, "PropertyName", StringComparison.Ordinal))!;
        var unit = number.FindDescendant<TextBlock>(part => string.Equals(part.Name, "PartValueUnit", StringComparison.Ordinal))!;
        var border = number.FindDescendant<Border>(part => string.Equals(part.Name, "PartBackgroundBorder", StringComparison.Ordinal))!;
        _ = label.FontSize.Should().Be(14 * textScale);
        _ = label.TextWrapping.Should().Be(TextWrapping.NoWrap);
        _ = label.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
        _ = number.ActualLabelPosition.Should().Be(LabelPosition.None, "the row shows the name");
        var inputPoint = border.TransformToVisual(card).TransformPoint(default);
        var unitPoint = unit.TransformToVisual(card).TransformPoint(default);
        _ = (unitPoint.X - inputPoint.X - border.ActualWidth - border.Margin.Right).Should().BeApproximately(4, 1);
        _ = (unitPoint.X + unit.ActualWidth).Should().BeLessThanOrEqualTo(width + 1);
        if (card.ActualLayout == PropertyLayout.Inline)
        {
            _ = (inputPoint.X - border.Margin.Left).Should().BeApproximately(((width - 12) * 0.4) + 12, 1);
        }
    });

    [TestMethod]
    [DataRow(280d, 1d)]
    [DataRow(480d, 1d)]
    [DataRow(760d, 1d)]
    [DataRow(480d, 2d)]
    [DataRow(760d, 2d)]
    public Task ColorFieldRowsGoInlineOnlyWhenTheirChannelsFitOnOneRow(double width, double textScale) => EnqueueAsync(async () =>
    {
        var host = CreateHost(width);
        var field = new ColorField { Label = "Color", FontSize = 14 * textScale };
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var card = CardOf(field);
        var vector = field.FindDescendant<VectorBox>()!;
        var header = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "HeaderQualifier", StringComparison.Ordinal))!;
        var suffix = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "ValueQualifier", StringComparison.Ordinal))!;

        _ = header.Visibility.Should().Be(card.ActualLayout == PropertyLayout.Stacked ? Visibility.Visible : Visibility.Collapsed);
        _ = suffix.Visibility.Should().Be(card.ActualLayout == PropertyLayout.Inline ? Visibility.Visible : Visibility.Collapsed);
        if (card.ActualLayout == PropertyLayout.Inline)
        {
            _ = vector.AreComponentsStacked.Should().BeFalse("an inline row has room for its channels on one row");
        }

        foreach (var channel in ShownChannels(vector))
        {
            _ = (channel.TransformToVisual(host).TransformPoint(default).X + channel.ActualWidth).Should().BeLessThanOrEqualTo(width + 1);
        }

        if (width == 280)
        {
            _ = card.ActualLayout.Should().Be(PropertyLayout.Stacked);
        }

        if (width == 760 && textScale == 1)
        {
            _ = card.ActualLayout.Should().Be(PropertyLayout.Inline);
        }
    });

    [TestMethod]
    public Task FieldFeedbackSitsBelowTheRowAndNeverMakesItStack() => EnqueueAsync(async () =>
    {
        var host = CreateHost(480);
        var rows = new StackPanel { Spacing = 4 };
        var number = CreateNumberField("Near plane", "m");
        var color = new ColorField { Label = "Color" };
        rows.Children.Add(number);
        rows.Children.Add(color);
        host.Children.Add(rows);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var layouts = new[] { CardOf(number).ActualLayout, CardOf(color).ActualLayout };

        number.ErrorText = LongError;
        number.HasError = true;
        number.ApplicabilityText = "Stored value. It applies in Auto exposure mode with Spot metering.";
        color.ErrorText = LongError;
        color.HasError = true;
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = new[] { CardOf(number).ActualLayout, CardOf(color).ActualLayout }.Should().Equal(layouts);
        AssertFeedbackBelow(number, number.ErrorText);
        AssertFeedbackBelow(number, number.ApplicabilityText);
        AssertFeedbackBelow(color, color.ErrorText);

        var error = number.FindDescendants().OfType<TextBlock>().Single(element => string.Equals(element.Text, number.ErrorText, StringComparison.Ordinal));
        var applicability = number.FindDescendants().OfType<TextBlock>().Single(element => string.Equals(element.Text, number.ApplicabilityText, StringComparison.Ordinal));
        number.HasError = false;
        number.ApplicabilityText = string.Empty;
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = error.Visibility.Should().Be(Visibility.Collapsed, "cleared feedback hides as the field's properties change");
        _ = applicability.Visibility.Should().Be(Visibility.Collapsed);
    });

    private static void AssertFeedbackBelow(Control field, string text)
    {
        var card = CardOf(field);
        var note = field.FindDescendants().OfType<TextBlock>().Single(element => string.Equals(element.Text, text, StringComparison.Ordinal));
        _ = note.Visibility.Should().Be(Visibility.Visible);
        _ = note.FindAscendant<PropertyCard>().Should().BeNull("feedback sits below the row, so its wrapping never decides whether the row stacks");
        _ = note.TransformToVisual(field).TransformPoint(default).Y.Should().BeGreaterThanOrEqualTo(card.TransformToVisual(field).TransformPoint(default).Y + card.ActualHeight);
    }

    private static NumberField CreateNumberField(string label, string unit)
        => new()
        {
            Label = label,
            Unit = unit,
            NumberValue = 2,
            FontSize = 14,
        };
}
