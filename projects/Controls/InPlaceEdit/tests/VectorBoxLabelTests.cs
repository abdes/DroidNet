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

namespace DroidNet.Controls.Tests;

/// <summary>Exercises optional, compact, colored component labels.</summary>
[TestClass]
[TestCategory("UITest")]
public sealed class VectorBoxLabelTests : VisualUserInterfaceTests
{
    [TestMethod]
    [DataRow(3)]
    [DataRow(4)]
    public Task EveryLabelAndComponentLabelPositionCanBeChosenAtRuntime(int dimension) => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { Dimension = dimension, Label = "Configurable", LabelPosition = LabelPosition.Left, AutoStackComponents = true, XValue = 1, YValue = 2, ZValue = 3, WValue = 4 };
        await LoadTestContentAsync(vector).ConfigureAwait(true);
        var positions = Enum.GetValues<LabelPosition>();

        foreach (var label in positions)
        {
            vector.LabelPosition = label;
            foreach (var component in positions)
            {
                vector.ComponentLabelPosition = component;
                vector.UpdateLayout();
                _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            }
        }

        _ = vector.GetValues().Should().Equal(new[] { 1d, 2, 3, 4 }.Take(dimension));
    });

    [TestMethod]
    public Task ComponentStyleAppliesToStandardNumberBoxesAndUpdatesAtRuntime() => EnqueueAsync(async () =>
    {
        var style = new Style(typeof(NumberBox));
        style.Setters.Add(new Setter(NumberBox.TrimTrailingZerosProperty, value: true));
        var vector = new VectorBox { Width = 180, Padding = new Thickness(0), ComponentStyle = style };
        await LoadTestContentAsync(vector).ConfigureAwait(true);
        var inputs = vector.FindDescendants().OfType<NumberBox>().ToArray();
        _ = inputs.Should().HaveCount(4, "the template holds X, Y, Z and the collapsed W editor");
        _ = inputs.Should().OnlyContain(input => ReferenceEquals(input.Style, style) && input.TrimTrailingZeros);
        _ = vector.ActualWidth.Should().BeApproximately(180, 1);
        var replacement = new Style(typeof(NumberBox));
        vector.ComponentStyle = replacement;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = inputs.Should().OnlyContain(input => ReferenceEquals(input.Style, replacement) && !input.TrimTrailingZeros);
    });

    [TestMethod]
    public Task CompactComponentLabelsRenderInsideTheirNumberBoxesAndCanBeColored() => EnqueueAsync(async () =>
    {
        var xBrush = new SolidColorBrush(Colors.Red);
        var yBrush = new SolidColorBrush(Colors.Green);
        var vector = new VectorBox { Dimension = 3, ComponentLabelPosition = LabelPosition.Left, CompactComponentLabels = true };
        vector.ComponentLabelForegrounds[Component.X] = xBrush;
        vector.ComponentLabelForegrounds[Component.Y] = yBrush;
        await LoadTestContentAsync(vector).ConfigureAwait(true);

        var xBox = vector.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        var xLabel = xBox.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartCompactLabelTextBlock", StringComparison.Ordinal))!;
        _ = xBox.IsCompact.Should().BeTrue();
        _ = xLabel.Text.Should().Be("X");
        _ = xLabel.Visibility.Should().Be(Visibility.Visible);
        _ = xLabel.Foreground.Should().BeSameAs(xBrush);
        _ = Grid.GetColumn(xLabel).Should().Be(0, "a left label comes before the value");

        var yBox = vector.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxY", StringComparison.Ordinal))!;
        var yLabel = yBox.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartCompactLabelTextBlock", StringComparison.Ordinal))!;
        _ = yLabel.Foreground.Should().BeSameAs(yBrush);

        vector.ComponentLabelPositions[Component.X] = LabelPosition.Right;
        xBox.UpdateLayout();

        _ = Grid.GetColumn(xLabel).Should().Be(2, "a right label comes after the value, still inside the field");
        _ = xLabel.Visibility.Should().Be(Visibility.Visible);
    });

    [TestMethod]
    [DataRow(LabelPosition.Left)]
    [DataRow(LabelPosition.Right)]
    [DataRow(LabelPosition.Top)]
    [DataRow(LabelPosition.Bottom)]
    public Task ComponentLabelsAreTheNumberBoxLabels(LabelPosition position) => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { Dimension = 3, ComponentLabelPosition = position, CompactComponentLabels = true };
        await LoadTestContentAsync(vector).ConfigureAwait(true);
        var xBox = vector.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        var outside = xBox.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartLabelTextBlock", StringComparison.Ordinal))!;
        var inside = xBox.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartCompactLabelTextBlock", StringComparison.Ordinal))!;
        var compact = position is LabelPosition.Left or LabelPosition.Right;

        _ = xBox.Label.Should().Be("X");
        _ = xBox.ActualLabelPosition.Should().Be(position);
        _ = inside.Visibility.Should().Be(compact ? Visibility.Visible : Visibility.Collapsed, "only a left or right label can be compact");
        _ = outside.Visibility.Should().Be(compact ? Visibility.Collapsed : Visibility.Visible);
    });

    [TestMethod]
    public Task ComponentLabelsRemainOptionalByDefault() => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { Dimension = 3 };
        await LoadTestContentAsync(vector).ConfigureAwait(true);

        var xBox = vector.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        _ = xBox.Label.Should().BeEmpty();
        _ = xBox.IsCompact.Should().BeFalse();
        _ = xBox.ActualLabelPosition.Should().Be(LabelPosition.None);
    });
}
