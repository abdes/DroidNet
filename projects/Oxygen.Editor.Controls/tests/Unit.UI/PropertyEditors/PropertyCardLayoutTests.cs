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
using NumberBox = DroidNet.Controls.NumberBox;
using static Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors.LayoutHosts;
using Oxygen.Editor.Controls.PropertyEditors;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors;

/// <summary>
///     The responsive layout of a property row: shared columns, ellipsis, qualifiers and their groups, and compound
///     editors, measured with real WinUI controls across widths and text scales.
/// </summary>
[TestClass]
internal sealed class PropertyCardLayoutTests : VisualUserInterfaceTests
{
    private static readonly string[] PropertyNames = ["Azimuth", "Elevation", "Angular diameter"];
    private static readonly double[] Widths = [760d, 620d, 900d, 760d];
    private static readonly double[] CompoundWidths = [760d, 480d, 420d, 340d, 280d, 760d];

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
                FontSize = 14 * textScale,
                Content = new NumberBox { NumberValue = 26.9, FontSize = 14 * textScale, Mask = "~.###", Unit = "°" },
            });
        }

        var host = CreateHost(width, rows);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        double? inputStart = null;
        double? unitStart = null;
        foreach (var card in rows.Children.OfType<PropertyCard>())
        {
            var number = (NumberBox)card.Content;
            var header = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PropertyName", StringComparison.Ordinal))!;
            var unit = number.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PartValueUnit", StringComparison.Ordinal))!;
            var field = number.FindDescendant<Border>(border => string.Equals(border.Name, "PartBackgroundBorder", StringComparison.Ordinal))!;
            var numberPoint = field.TransformToVisual(card).TransformPoint(default);
            var unitPoint = unit.TransformToVisual(card).TransformPoint(default);
            var expected = width >= 12 + (124 * textScale / 0.4) ? PropertyLayout.Inline : PropertyLayout.Stacked;
            _ = card.ActualLayout.Should().Be(expected);
            _ = header.Text.Should().Be(card.PropertyName, "the row, not the NumberBox, shows the property name");
            _ = header.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
            _ = number.ActualLabelPosition.Should().Be(LabelPosition.None);
            _ = unit.Text.Should().Be("°");
            _ = unitPoint.X.Should().BeGreaterThanOrEqualTo(numberPoint.X + field.ActualWidth, "the unit follows the value field");
            _ = (unitPoint.X + unit.ActualWidth).Should().BeLessThanOrEqualTo(width + 1);
            if (expected == PropertyLayout.Stacked)
            {
                _ = numberPoint.Y.Should().BeGreaterThanOrEqualTo(header.ActualHeight + 3);
            }
            else
            {
                _ = (numberPoint.X - field.Margin.Left).Should().BeApproximately(((width - 12) * 0.4) + 12, 1);
            }

            _ = numberPoint.X.Should().BeApproximately(inputStart ?? numberPoint.X, 1);
            _ = unitPoint.X.Should().BeApproximately(unitStart ?? unitPoint.X, 1);
            inputStart = numberPoint.X;
            unitStart = unitPoint.X;
        }
    });

    [TestMethod]
    public Task MixedInlineRowsKeepTheSameProportionsAndEllipsisWhileResizing() => EnqueueAsync(async () =>
    {
        const string longLabel = "A very long inspector property label that must stay on a single line";
        var scalar = new PropertyCard { PropertyName = longLabel, Content = new NumberBox { NumberValue = 26.9, Mask = "~.###", Unit = "°" } };
        var toggle = new PropertyCard { PropertyName = "Enabled", Content = new ToggleSwitch() };
        var compound = new PropertyCard
        {
            PropertyName = longLabel,
            IsCompound = true,
            Qualifier = "m",
            Content = new VectorBox { ComponentLabelPosition = LabelPosition.Left, CompactComponentLabels = true, ComponentMask = "~.###" },
        };
        var rows = new StackPanel { Spacing = 4 };
        rows.Children.Add(scalar);
        rows.Children.Add(toggle);
        rows.Children.Add(compound);
        var host = CreateHost(760, rows);
        await LoadTestContentAsync(host).ConfigureAwait(true);

        foreach (var width in Widths)
        {
            host.Width = width;
            host.UpdateLayout();
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            var labelWidth = (width - 12) * 0.4;
            var valueWidth = (width - 12) * 0.6;
            foreach (var card in new[] { scalar, toggle, compound })
            {
                var label = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PropertyName", StringComparison.Ordinal))!;
                var presenter = card.FindDescendant<ContentPresenter>(part => string.Equals(part.Name, "PropertyEditor", StringComparison.Ordinal))!;
                var valueGroup = (Grid)presenter.Parent;
                _ = card.ActualLayout.Should().Be(PropertyLayout.Inline);
                _ = valueGroup.TransformToVisual(rows).TransformPoint(default).X.Should().BeApproximately(labelWidth + 12, 1);
                _ = valueGroup.ActualWidth.Should().BeApproximately(valueWidth, 1);
                _ = label.TextWrapping.Should().Be(TextWrapping.NoWrap);
                _ = label.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
                if (!ReferenceEquals(card, toggle))
                {
                    _ = label.IsTextTrimmed.Should().BeTrue();
                    _ = ToolTipService.GetToolTip(label).Should().Be(longLabel);
                }
            }
        }
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
        var position = string.Equals(kind, "position", StringComparison.Ordinal);
        var card = CompoundCard(kind, textScale);
        var host = CreateHost(760, card);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var vector = card.Content as VectorBox ?? ((Control)card.Content).FindDescendant<VectorBox>()!;
        var inputs = ShownChannels(vector);
        foreach (var input in inputs)
        {
            input.FontSize = 14 * textScale;
        }

        foreach (var width in CompoundWidths)
        {
            host.Width = width;
            host.UpdateLayout();
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            var headerQualifier = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "HeaderQualifier", StringComparison.Ordinal))!;
            var valueQualifier = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "ValueQualifier", StringComparison.Ordinal))!;
            _ = headerQualifier.Visibility.Should().Be(card.ActualLayout == PropertyLayout.Stacked ? Visibility.Visible : Visibility.Collapsed);
            _ = valueQualifier.Visibility.Should().Be(card.ActualLayout == PropertyLayout.Inline ? Visibility.Visible : Visibility.Collapsed);
            _ = inputs.Should().HaveCount(3);
            _ = inputs.Select(input => input.Label).Should().Equal(position ? ["X", "Y", "Z"] : ["R", "G", "B"]);
            _ = inputs.Should().OnlyContain(input => AutomationProperties.GetName(input).Contains(position ? "metres" : "RGB", StringComparison.Ordinal));
            _ = vector.FindDescendants().OfType<NumberBox>().Take(3).Should().Equal(inputs);
            AssertChannelsFitInOrder(card, vector, inputs, width, textScale);

            if (width == 280 && textScale > 1)
            {
                _ = vector.AreComponentsStacked.Should().BeTrue();
            }

            if (width == 760 && textScale == 1 && position)
            {
                _ = card.ActualLayout.Should().Be(PropertyLayout.Inline);
            }
        }
    });

    [TestMethod]
    [DataRow(760d)]
    [DataRow(420d)]
    public Task RowsInAQualifierGroupEndTheirEditorsAtOneEdge(double width) => EnqueueAsync(async () =>
    {
        var position = Compound("Position", "m");
        var rotation = Compound("Rotation", "°");
        var scale = Compound("Scale", string.Empty);
        var intensity = new PropertyCard { PropertyName = "Intensity", QualifierGroup = "Transform", Content = new NumberBox { NumberValue = 2, Unit = "lux" } };
        var outsider = Compound("Outsider", string.Empty);
        outsider.QualifierGroup = string.Empty;
        var rows = new StackPanel { Spacing = 4 };
        foreach (var row in new[] { position, rotation, scale, intensity, outsider })
        {
            rows.Children.Add(row);
        }

        var host = CreateHost(width, rows);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

        var edges = new[] { position, rotation, scale, intensity }.Select(EditorRightEdge).ToArray();
        _ = edges.Should().AllSatisfy(edge => edge.Should().BeApproximately(edges[0], 1), "the group reserves its widest annotation after every editor");
        _ = EditorRightEdge(outsider).Should().BeGreaterThan(edges[0] + 1, "a row outside the group reserves nothing");

        intensity.Visibility = Visibility.Collapsed;
        host.UpdateLayout();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

        var remaining = new[] { position, rotation, scale }.Select(EditorRightEdge).ToArray();
        _ = remaining.Should().AllSatisfy(edge => edge.Should().BeApproximately(remaining[0], 1));
        _ = remaining[0].Should().BeGreaterThan(edges[0], "a hidden row no longer widens the group");

        double EditorRightEdge(PropertyCard card)
        {
            var editor = card.Content is NumberBox number
                ? number.FindDescendant<Border>(border => string.Equals(border.Name, "PartBackgroundBorder", StringComparison.Ordinal))!
                : (FrameworkElement)card.Content;
            return editor.TransformToVisual(rows).TransformPoint(default).X + editor.ActualWidth + editor.Margin.Right;
        }

        static PropertyCard Compound(string name, string qualifier) => new()
        {
            PropertyName = name,
            IsCompound = true,
            Layout = PropertyLayout.Inline,
            Qualifier = qualifier,
            QualifierGroup = "Transform",
            Content = new VectorBox { ComponentLabelPosition = LabelPosition.Left, CompactComponentLabels = true, ComponentMask = "~.###" },
        };
    });

    [TestMethod]
    public Task MeasuringARowNeverRearrangesItsStackedVector() => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { ComponentLabelPosition = LabelPosition.Left, CompactComponentLabels = true, AutoStackComponents = true, ComponentMask = "~.###" };
        var card = new PropertyCard { PropertyName = "Position", IsCompound = true, Qualifier = "m", Content = vector };
        var host = CreateHost(220, card);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var components = vector.FindDescendant<Grid>(part => string.Equals(part.Name, "PartComponentPanel", StringComparison.Ordinal))!;
        _ = vector.AreComponentsStacked.Should().BeTrue("the row is narrower than the components on one row");
        var rows = components.RowDefinitions.ToArray();
        var width = vector.PreferredWidth;

        card.InvalidateMeasure();
        host.UpdateLayout();

        _ = vector.AreComponentsStacked.Should().BeTrue();
        _ = components.RowDefinitions.Should().Equal(rows, "the row reads the vector's preferred width instead of measuring it unconstrained");
        _ = vector.PreferredWidth.Should().Be(width);
        _ = card.ActualLayout.Should().Be(PropertyLayout.Stacked);
    });

    [TestMethod]
    public Task QualifierGroupsStayWithinTheirSection() => EnqueueAsync(async () =>
    {
        var position = GroupRow("Position", "m");
        var scale = GroupRow("Scale", string.Empty);
        var color = GroupRow("Color", "Linear RGBA");
        var transform = new PropertiesExpander { Header = "Transform", IsExpanded = true };
        transform.Items.Add(position);
        transform.Items.Add(scale);
        var light = new PropertiesExpander { Header = "Light", IsExpanded = true };
        light.Items.Add(color);
        var sections = new StackPanel { Spacing = 4 };
        sections.Children.Add(transform);
        sections.Children.Add(light);
        var host = CreateHost(760, sections);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = ValueGroup(position).Margin.Right.Should().Be(0, "the widest annotation of its own section reserves nothing more");
        _ = ValueGroup(scale).Margin.Right.Should().BeGreaterThan(0, "a row without a qualifier reserves its section's widest");
        _ = ValueGroup(color).Margin.Right.Should().Be(0);

        static PropertyCard GroupRow(string name, string qualifier) => new()
        {
            PropertyName = name,
            IsCompound = true,
            Layout = PropertyLayout.Inline,
            Qualifier = qualifier,
            QualifierGroup = "Transform",
            Content = new VectorBox { ComponentLabelPosition = LabelPosition.Left, CompactComponentLabels = true, ComponentMask = "~.###" },
        };

        static Grid ValueGroup(PropertyCard card)
            => (Grid)card.FindDescendant<ContentPresenter>(part => string.Equals(part.Name, "PropertyEditor", StringComparison.Ordinal))!.Parent;
    });

    // A compound row as the inspector composes it: a position vector, a linear color, or an RGB multiplier.
    private static PropertyCard CompoundCard(string kind, double textScale)
    {
        var position = string.Equals(kind, "position", StringComparison.Ordinal);
        var color = string.Equals(kind, "color", StringComparison.Ordinal);
        Control editor = position
            ? new VectorBox { ComponentLabelPosition = LabelPosition.Left, CompactComponentLabels = true, ComponentMask = "~.###", FontSize = 14 * textScale }
            : new ColorBox
            {
                Encoding = ColorEncoding.Linear,
                Range = color ? ColorRange.Normalized : ColorRange.HighDynamicRange,
                IsPickerVisible = color,
                FontSize = 14 * textScale,
            };
        AutomationProperties.SetName(editor, position ? "Position, metres" : $"{kind}, RGB");
        return new PropertyCard
        {
            PropertyName = position ? "Position" : color ? "Color" : "Disk luminance scale",
            IsCompound = true,
            Qualifier = position ? "m" : color ? "Linear RGB" : "RGB ×",
            Layout = position ? PropertyLayout.Auto : PropertyLayout.Stacked,
            FontSize = 14 * textScale,
            Content = editor,
        };
    }

    // Channels keep a usable equal width inside the row, in order along a row or down a column.
    private static void AssertChannelsFitInOrder(PropertyCard card, VectorBox vector, NumberBox[] inputs, double width, double textScale)
    {
        var points = inputs.Select(input => input.TransformToVisual(card).TransformPoint(default)).ToArray();
        for (var i = 0; i < inputs.Length; i++)
        {
            _ = inputs[i].ActualWidth.Should().BeGreaterThanOrEqualTo(
                (80 * textScale) - 1,
                $"dock width is {width}, vector width is {vector.ActualWidth}, vertical channels are {vector.AreComponentsStacked}");
            _ = (points[i].X + inputs[i].ActualWidth).Should().BeLessThanOrEqualTo(width + 1);
            _ = inputs[i].ActualWidth.Should().BeApproximately(inputs[0].ActualWidth, 1);
            if (i == 0)
            {
                continue;
            }

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
}
