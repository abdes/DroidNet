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
using Oxygen.Editor.Controls.PropertyEditors;
using static Oxygen.Editor.Controls.Testing.PropertyEditorProbes;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors;

/// <summary>A color field is a property row around a linear ColorBox whose kind decides its qualifier, range and picker.</summary>
[TestClass]
internal sealed class ColorFieldTests : VisualUserInterfaceTests
{
    [TestMethod]
    [DataRow(ColorFieldKind.Linear, false, "Linear RGB")]
    [DataRow(ColorFieldKind.Linear, true, "Linear RGBA")]
    [DataRow(ColorFieldKind.Luminance, false, "Luminance RGB")]
    [DataRow(ColorFieldKind.Multiplier, false, "RGB ×")]
    public Task TheKindDecidesTheQualifierRangeAndPicker(ColorFieldKind kind, bool hasAlpha, string qualifier) => EnqueueAsync(async () =>
    {
        var host = CreateHost(480);
        var field = new ColorField { Kind = kind, HasAlpha = hasAlpha, Label = "Color", Red = 0.25, Green = 0.5, Blue = 1 };
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var box = field.FindDescendant<ColorBox>()!;

        _ = field.Qualifier.Should().Be(qualifier);
        _ = CardOf(field).Qualifier.Should().Be(qualifier);
        _ = box.Encoding.Should().Be(ColorEncoding.Linear);
        _ = box.Range.Should().Be(kind == ColorFieldKind.Linear ? ColorRange.Normalized : ColorRange.HighDynamicRange);
        _ = box.IsPickerVisible.Should().Be(kind != ColorFieldKind.Multiplier, "a multiplier only scales other colors");
        _ = Channels(field).Select(channel => channel.Label).Should().Equal(hasAlpha ? ["R", "G", "B", "A"] : ["R", "G", "B"]);
    });

    [TestMethod]
    public Task MixedChannelsKindChangesAndFeedbackKeepTheSameInputs() => EnqueueAsync(async () =>
    {
        var host = CreateHost(480);
        var field = new ColorField { Red = 0.25, Green = 0.5, Blue = 1, RedIsMixed = true, PickerAutomationName = "Pick ground albedo color" };
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var inputs = Channels(field);

        _ = inputs.Select(input => input.IsIndeterminate).Should().Equal(true, false, false);
        _ = AutomationProperties.GetName(SwatchOf(field)).Should().Be("Pick ground albedo color");

        field.Kind = ColorFieldKind.Multiplier;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = SwatchOf(field).Visibility.Should().Be(Visibility.Collapsed);
        _ = Channels(field).Should().Equal(inputs);

        field.ErrorText = "Ground albedo must be between 0 and 1.";
        field.HasError = true;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = field.FindDescendants().OfType<TextBlock>().Should().Contain(note => note.Text == field.ErrorText && note.Visibility == Visibility.Visible);
        _ = new[] { field.Red, field.Green, field.Blue }.Should().Equal(0.25, 0.5, 1);
    });

    [TestMethod]
    public Task TheFieldAndItsColorBoxStayEqualAfterTheBoxIsEdited() => EnqueueAsync(async () =>
    {
        // Contract: an edit of a channel reaches the field, and a later value of the field still reaches the box.
        var host = CreateHost(480);
        var field = new ColorField { HasAlpha = true, Red = 0.25, Green = 0.5, Blue = 1, Alpha = 1 };
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var box = field.FindDescendant<ColorBox>()!;

        box.Red = 0.75;
        box.Alpha = 0.5;
        _ = new[] { field.Red, field.Alpha }.Should().Equal(0.75, 0.5);

        field.Red = 0.25;
        field.Alpha = 1;
        _ = new[] { box.Red, box.Green, box.Blue, box.Alpha }.Should().Equal(0.25, 0.5, 1, 1);
    });

    private static NumberBox[] Channels(ColorField field)
        => [.. field.FindDescendants().OfType<NumberBox>().Where(input => input.FindAscendant<Grid>() is { Visibility: Visibility.Visible })];

    private static Grid CreateHost(double width)
        => new() { Width = width, HorizontalAlignment = HorizontalAlignment.Left, RequestedTheme = ElementTheme.Light };
}
