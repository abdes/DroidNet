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
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Controls;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Controls;
using NumberBox = DroidNet.Controls.NumberBox;
using PropertyCard = Oxygen.Editor.Controls.PropertyCard;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
internal sealed class InspectorRgbFieldTests : VisualUserInterfaceTests
{
    [TestMethod]
    [DataRow(false, 280d, 1d)]
    [DataRow(false, 480d, 1d)]
    [DataRow(false, 420d, 1.5d)]
    [DataRow(false, 480d, 2d)]
    [DataRow(false, 760d, 2d)]
    [DataRow(true, 280d, 1d)]
    [DataRow(true, 480d, 1d)]
    [DataRow(true, 420d, 1.5d)]
    [DataRow(true, 480d, 2d)]
    [DataRow(true, 760d, 2d)]
    public Task RgbCompositionPreservesDirectCardGeometryAndTypography(bool multiplier, double width, double textScale) => EnqueueAsync(async () =>
    {
        var host = CreateHost(width);
        var field = new InspectorRgbField
        {
            FontSize = 14 * textScale,
            IsMultiplier = multiplier,
            Label = multiplier ? "Sky Luminance" : "Color",
            Qualifier = multiplier ? "RGB ×" : "Linear RGB",
            Red = 0.25f,
            Green = 0.5f,
            Blue = 1,
        };
        host.Children.Add(field);
        using var nativeHost = new ScaledXamlHost();
        await nativeHost.LoadAsync(host, 1, CancellationToken.None).ConfigureAwait(true);
        var actualCard = (PropertyCard)field.Content;
        var actualVector = field.FindDescendant<VectorBox>()!;
        ScaleChannels(actualVector, textScale);
        await WaitForRenderAsync().ConfigureAwait(true);
        var actual = Measure(actualCard, actualVector);

        host.Children.Clear();
        var vector = new VectorBox
        {
            ComponentLabelPosition = LabelPosition.Left,
            ComponentMask = "~.###",
            LabelPosition = LabelPosition.None,
            XValue = 0.25f,
            YValue = 0.5f,
            ZValue = 1,
        };
        InspectorRgbPresentation.Configure(vector);
        var reference = new PropertyCard
        {
            PropertyName = field.Label,
            Qualifier = field.Qualifier,
            FontSize = field.FontSize,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            EditorMinimumWidth = 248,
            IsCompound = true,
            Layout = PropertyLayout.Stacked,
        };
        var panel = new StackPanel { Spacing = 4 };
        panel.Children.Add(vector);
        panel.Children.Add(new TextBlock { Visibility = Visibility.Collapsed });
        reference.Content = panel;
        if (!multiplier)
        {
            reference.LeadingContent = new Button
            {
                Style = (Style)host.Resources["InspectorColorSwatchStyle"],
                Content = new Border
                {
                    Width = 20,
                    Height = 24,
                    Background = InspectorRgbPresentation.ToBrush(new(0.25f, 0.5f, 1)),
                    BorderBrush = ((Border)((Button)actualCard.LeadingContent!).Content).BorderBrush,
                    BorderThickness = new Thickness(1),
                    CornerRadius = new CornerRadius(2),
                },
            };
        }

        host.Children.Add(reference);
        await WaitForRenderAsync().ConfigureAwait(true);
        ScaleChannels(vector, textScale);
        await WaitForRenderAsync().ConfigureAwait(true);
        var expected = Measure(reference, vector);
        _ = actual.Should().HaveCount(expected.Length);
        for (var index = 0; index < expected.Length; index++)
        {
            _ = actual[index].Should().BeApproximately(expected[index], 1);
        }
    });

    [TestMethod]
    public Task RgbCompositionAccessoryMetadataAndMixedChannelsUpdateWithoutReplacingInputs() => EnqueueAsync(async () =>
    {
        var host = CreateHost(480);
        var field = new InspectorRgbField { Red = 0.25f, Green = 0.5f, Blue = 1, RedIsMixed = true };
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var card = (PropertyCard)field.Content;
        var inputs = field.FindDescendants().OfType<NumberBox>().ToArray();
        _ = inputs.Select(input => input.IsIndeterminate).Should().Equal(true, false, false);
        var swatch = (Button)card.LeadingContent!;
        var border = (Border)swatch.Content;
        var replacementBrush = new SolidColorBrush(Microsoft.UI.Colors.Red);
        field.SwatchBorderBrush = replacementBrush;
        field.SwatchCornerRadius = new CornerRadius(3);
        field.PickerAutomationName = "Pick ground albedo color";
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = border.BorderBrush.Should().BeSameAs(replacementBrush);
        _ = border.CornerRadius.Should().Be(new CornerRadius(3));
        _ = AutomationProperties.GetName(swatch).Should().Be("Pick ground albedo color");
        field.IsMultiplier = true;
        host.Width = 280;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = card.LeadingContent.Should().BeNull();
        _ = field.FindDescendants().OfType<NumberBox>().Should().Equal(inputs);
        field.IsMultiplier = false;
        field.SwatchBorderBrush = null;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = card.LeadingContent.Should().BeSameAs(swatch);
        _ = border.BorderBrush.Should().NotBeSameAs(replacementBrush);
        _ = new[] { field.Red, field.Green, field.Blue }.Should().Equal(0.25f, 0.5f, 1);
    });

    private static Grid CreateHost(double width)
    {
        var host = new Grid { Width = width, Height = 480, HorizontalAlignment = HorizontalAlignment.Left, RequestedTheme = ElementTheme.Light };
        host.Resources.MergedDictionaries.Add(new ResourceDictionary
        {
            Source = new Uri("ms-appx:///Oxygen.Editor.WorldEditor/Inspector/SceneInspectorStyles.xaml"),
        });
        return host;
    }

    private static void ScaleChannels(VectorBox vector, double scale)
    {
        vector.FontSize = 14 * scale;
        foreach (var channel in vector.FindDescendants().OfType<NumberBox>())
        {
            channel.FontSize = 14 * scale;
        }
    }

    private static double[] Measure(PropertyCard card, VectorBox vector)
    {
        var measurements = new List<double> { card.ActualWidth, card.ActualHeight };
        var inputs = vector.FindDescendants().OfType<NumberBox>().ToArray();
        _ = inputs.Select(input => input.Label).Should().Equal("R", "G", "B");
        foreach (var input in inputs)
        {
            var point = input.TransformToVisual(card).TransformPoint(default);
            measurements.AddRange([point.X, point.Y, input.ActualWidth, input.ActualHeight, input.FontSize]);
        }

        return [.. measurements];
    }
}
