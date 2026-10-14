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
using Oxygen.Editor.Controls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using NumberBox = DroidNet.Controls.NumberBox;
using PropertyCard = Oxygen.Editor.Controls.PropertyCard;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

/// <summary>Exercises the inspector color row: a property card around a linear ColorBox.</summary>
[TestClass]
internal sealed class InspectorColorFieldTests : VisualUserInterfaceTests
{
    [TestMethod]
    [DataRow(InspectorColorKind.Linear, false, "Linear RGB")]
    [DataRow(InspectorColorKind.Linear, true, "Linear RGBA")]
    [DataRow(InspectorColorKind.Luminance, false, "Luminance RGB")]
    [DataRow(InspectorColorKind.Multiplier, false, "RGB ×")]
    public Task TheKindDecidesTheQualifierRangeAndPicker(InspectorColorKind kind, bool hasAlpha, string qualifier) => EnqueueAsync(async () =>
    {
        var host = CreateHost(480);
        var field = new InspectorColorField { Kind = kind, HasAlpha = hasAlpha, Label = "Color", Red = 0.25, Green = 0.5, Blue = 1 };
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var box = field.FindDescendant<ColorBox>()!;

        _ = field.Qualifier.Should().Be(qualifier);
        _ = CardOf(field).Qualifier.Should().Be(qualifier);
        _ = box.Encoding.Should().Be(ColorEncoding.Linear);
        _ = box.Range.Should().Be(kind == InspectorColorKind.Linear ? ColorRange.Normalized : ColorRange.HighDynamicRange);
        _ = box.IsPickerVisible.Should().Be(kind != InspectorColorKind.Multiplier, "a multiplier only scales other colors");
        _ = Channels(field).Select(channel => channel.Label).Should().Equal(hasAlpha ? ["R", "G", "B", "A"] : ["R", "G", "B"]);
    });

    [TestMethod]
    public Task MixedChannelsKindChangesAndFeedbackKeepTheSameInputs() => EnqueueAsync(async () =>
    {
        var host = CreateHost(480);
        var field = new InspectorColorField { Red = 0.25, Green = 0.5, Blue = 1, RedIsMixed = true, PickerAutomationName = "Pick ground albedo color" };
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var inputs = Channels(field);

        _ = inputs.Select(input => input.IsIndeterminate).Should().Equal(true, false, false);
        _ = AutomationProperties.GetName(SwatchOf(field)).Should().Be("Pick ground albedo color");

        field.Kind = InspectorColorKind.Multiplier;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = SwatchOf(field).Visibility.Should().Be(Visibility.Collapsed);
        _ = Channels(field).Should().Equal(inputs);

        field.ErrorText = "Ground albedo must be between 0 and 1.";
        field.HasError = true;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = field.FindDescendants().OfType<TextBlock>().Should().Contain(note => note.Text == field.ErrorText && note.Visibility == Visibility.Visible);
        _ = new[] { field.Red, field.Green, field.Blue }.Should().Equal(0.25, 0.5, 1);
    });

    private static NumberBox[] Channels(InspectorColorField field)
        => [.. field.FindDescendants().OfType<NumberBox>().Where(input => input.FindAscendant<Grid>() is { Visibility: Visibility.Visible })];

    private static Grid CreateHost(double width)
    {
        var host = new Grid { Width = width, HorizontalAlignment = HorizontalAlignment.Left, RequestedTheme = ElementTheme.Light };
        host.Resources.MergedDictionaries.Add(new ResourceDictionary
        {
            Source = new Uri("ms-appx:///Oxygen.Editor.WorldEditor/Inspector/SceneInspectorStyles.xaml"),
        });
        return host;
    }
}
