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
    public Task ComponentStyleAppliesToStandardNumberBoxesAndUpdatesAtRuntime() => EnqueueAsync(async () =>
    {
        var style = new Style(typeof(NumberBox));
        style.Setters.Add(new Setter(NumberBox.TrimTrailingZerosProperty, value: true));
        var vector = new VectorBox { Width = 180, Padding = new Thickness(0), ComponentStyle = style };
        await LoadTestContentAsync(vector).ConfigureAwait(true);
        var inputs = vector.FindDescendants().OfType<NumberBox>().ToArray();
        _ = inputs.Should().HaveCount(3);
        _ = inputs.Should().OnlyContain(input => ReferenceEquals(input.Style, style) && input.TrimTrailingZeros);
        _ = vector.ActualWidth.Should().BeApproximately(180, 1);
        var replacement = new Style(typeof(NumberBox));
        vector.ComponentStyle = replacement;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = inputs.Should().OnlyContain(input => ReferenceEquals(input.Style, replacement) && !input.TrimTrailingZeros);
    });

    [TestMethod]
    public Task LeftComponentLabelsRenderInsideCompactNumberBoxesAndCanBeColored() => EnqueueAsync(async () =>
    {
        var xBrush = new SolidColorBrush(Colors.Red);
        var yBrush = new SolidColorBrush(Colors.Green);
        var vector = new VectorBox { Dimension = 3, ComponentLabelPosition = LabelPosition.Left };
        vector.ComponentLabelForegrounds["X"] = xBrush;
        vector.ComponentLabelForegrounds["Y"] = yBrush;
        await LoadTestContentAsync(vector).ConfigureAwait(true);

        var xBox = vector.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        var xLabel = xBox.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartCompactLabelTextBlock", StringComparison.Ordinal))!;
        _ = xBox.IsCompact.Should().BeTrue();
        _ = xLabel.Text.Should().Be("X");
        _ = xLabel.Visibility.Should().Be(Visibility.Visible);
        _ = xLabel.Foreground.Should().BeSameAs(xBrush);
        _ = vector.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartLabelX", StringComparison.Ordinal))!.Visibility.Should().Be(Visibility.Collapsed);

        var yBox = vector.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxY", StringComparison.Ordinal))!;
        var yLabel = yBox.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartCompactLabelTextBlock", StringComparison.Ordinal))!;
        _ = yLabel.Foreground.Should().BeSameAs(yBrush);
    });

    [TestMethod]
    public Task ComponentLabelsRemainOptionalByDefault() => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { Dimension = 3 };
        await LoadTestContentAsync(vector).ConfigureAwait(true);

        var xBox = vector.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        _ = xBox.Label.Should().BeEmpty();
        _ = xBox.IsCompact.Should().BeFalse();
        _ = vector.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartLabelX", StringComparison.Ordinal))!.Visibility.Should().Be(Visibility.Collapsed);
    });
}
