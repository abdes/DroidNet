// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace DroidNet.Controls.Tests;

/// <summary>Exercises the color box's display conversion, channels, swatch and picks.</summary>
[TestClass]
[TestCategory("UITest")]
public sealed class ColorBoxTests : VisualUserInterfaceTests
{
    [TestMethod]
    public void LinearChannelsShowAndPickAsSrgbAndAlphaIsNotConverted()
    {
        var display = ColorPresentation.ToDisplayColor(new ColorChannels(0.5, 0, 1, 0.5), ColorEncoding.Linear, ColorRange.Normalized);
        var picked = ColorPresentation.FromDisplayColor(Color.FromArgb(128, 188, 0, 255), ColorEncoding.Linear, 1);

        _ = display.Should().Be(Color.FromArgb(128, 188, 0, 255));
        _ = picked.Red.Should().BeApproximately(0.5029, 0.0001);
        _ = picked.Alpha.Should().Be(128 / 255d);
        _ = ColorPresentation.ToDisplayColor(new ColorChannels(0.5, 0, 1, 1), ColorEncoding.Srgb, ColorRange.Normalized).R.Should().Be(128);
    }

    [TestMethod]
    public void HighDynamicRangeShowsTheHueAndPicksKeepTheBrightness()
    {
        var color = new ColorChannels(4, 2, 0, 1);
        var intensity = ColorPresentation.Intensity(color, ColorRange.HighDynamicRange);
        var display = ColorPresentation.ToDisplayColor(color, ColorEncoding.Srgb, ColorRange.HighDynamicRange);
        var picked = ColorPresentation.ApplyPick(color, Color.FromArgb(255, 255, 255, 0), ColorEncoding.Srgb, intensity);

        _ = intensity.Should().Be(4);
        _ = display.Should().Be(Color.FromArgb(255, 255, 128, 0));
        _ = picked.Should().Be(new ColorChannels(4, 4, 0, 1));
        _ = ColorPresentation.Intensity(new ColorChannels(0.5, 0.2, 0, 1), ColorRange.HighDynamicRange).Should().Be(1);
    }

    [TestMethod]
    public void APickKeepsTheChannelsItDidNotChange()
    {
        var color = new ColorChannels(0.123456, 0.654321, 0.333333, 0.777777);
        var shown = ColorPresentation.ToDisplayColor(color, ColorEncoding.Linear, 1);

        var picked = ColorPresentation.ApplyPick(color, Color.FromArgb(shown.A, 255, shown.G, shown.B), ColorEncoding.Linear, 1);

        _ = picked.Should().Be(color with { Red = 1 });
    }

    [TestMethod]
    public Task ChannelsAreLabelledAndAlphaShowsOnlyWhenEnabled() => EnqueueAsync(async () =>
    {
        var box = new ColorBox { Red = 0.25, Green = 0.5, Blue = 1, Alpha = 0.5 };
        await LoadTestContentAsync(box).ConfigureAwait(true);

        _ = ShownChannels(box).Select(static channel => (channel.Label, channel.NumberValue)).Should().Equal(("R", 0.25), ("G", 0.5), ("B", 1d));
        _ = Part<Grid>(box, "PartSwatchAlpha").Visibility.Should().Be(Visibility.Collapsed);

        box.HasAlpha = true;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

        _ = ShownChannels(box).Select(static channel => (channel.Label, channel.NumberValue)).Should().Equal(("R", 0.25), ("G", 0.5), ("B", 1d), ("A", 0.5));
        _ = Part<Grid>(box, "PartSwatchAlpha").Visibility.Should().Be(Visibility.Visible);
    });

    [TestMethod]
    public Task TheSwatchFollowsTheChannelsAndShowsMixedSelections() => EnqueueAsync(async () =>
    {
        var box = new ColorBox { Encoding = ColorEncoding.Linear, Red = 0.5 };
        await LoadTestContentAsync(box).ConfigureAwait(true);

        _ = SwatchColor(box).Should().Be(Color.FromArgb(255, 188, 0, 0));

        ShownChannels(box)[1].NumberValue = 1;
        _ = SwatchColor(box).Should().Be(Color.FromArgb(255, 188, 255, 0));
        _ = box.Green.Should().Be(1);

        box.BlueIsIndeterminate = true;
        _ = Part<Border>(box, "PartSwatchFill").Background.Should().BeNull();
        _ = Part<TextBlock>(box, "PartSwatchMixed").Visibility.Should().Be(Visibility.Visible);
    });

    [TestMethod]
    public Task AnUnhandledPickSetsTheChannelsInOneSession() => EnqueueAsync(async () =>
    {
        var box = new ColorBox { Encoding = ColorEncoding.Linear, Alpha = 0.3 };
        var sessions = new List<(string kind, Component? channel, NumberBoxEditCompletionKind? completion)>();
        box.EditSessionStarted += (_, args) => sessions.Add(("Started", args.Channel, args.CompletionKind));
        box.EditSessionCompleted += (_, args) => sessions.Add(("Completed", args.Channel, args.CompletionKind));
        await LoadTestContentAsync(box).ConfigureAwait(true);

        await PickAsync(box, Color.FromArgb(255, 188, 0, 255)).ConfigureAwait(true);

        _ = box.Red.Should().BeApproximately(0.5029, 0.0001);
        _ = box.Blue.Should().Be(1);
        _ = box.Alpha.Should().Be(0.3, "a color without alpha keeps its alpha");
        _ = sessions.Should().Equal(("Started", null, null), ("Completed", null, NumberBoxEditCompletionKind.Commit));
    });

    [TestMethod]
    public Task AHandledPickLeavesTheChannelsToTheHandler() => EnqueueAsync(async () =>
    {
        var box = new ColorBox { HasAlpha = true };
        var picks = new List<ColorChannels>();
        box.ColorPicked += (_, args) =>
        {
            picks.Add(args.Color);
            args.Handled = true;
        };
        await LoadTestContentAsync(box).ConfigureAwait(true);

        await PickAsync(box, Color.FromArgb(128, 255, 0, 0)).ConfigureAwait(true);

        _ = picks.Should().ContainSingle().Which.Should().Be(new ColorChannels(1, 0, 0, 128 / 255d));
        _ = box.Channels.Should().Be(new ColorChannels(0, 0, 0, 1));
    });

    [TestMethod]
    public Task AHiddenPickerLeavesOnlyTheChannels() => EnqueueAsync(async () =>
    {
        var box = new ColorBox { IsPickerVisible = false };
        await LoadTestContentAsync(box).ConfigureAwait(true);

        _ = Part<Button>(box, "PartSwatchButton").Visibility.Should().Be(Visibility.Collapsed);
        _ = Part<VectorBox>(box, "PartChannels").TransformToVisual(box).TransformPoint(default).X.Should().Be(0);
    });

    [TestMethod]
    public Task ChannelInputStaysInTheColorRange() => EnqueueAsync(async () =>
    {
        var box = new ColorBox { HasAlpha = true };
        await LoadTestContentAsync(box).ConfigureAwait(true);

        _ = ShownChannels(box).Select(static channel => (channel.Minimum, channel.Maximum)).Should().AllBeEquivalentTo((0d, 1d));
        _ = ShownChannels(box)[0].Multiplier.Should().Be(0.01d, "a 0 to 1 channel drags in hundredths");

        box.Range = ColorRange.HighDynamicRange;

        _ = ShownChannels(box).Select(static channel => (channel.Minimum, channel.Maximum))
            .Should().Equal((0d, double.PositiveInfinity), (0d, double.PositiveInfinity), (0d, double.PositiveInfinity), (0d, 1d));
    });

    [TestMethod]
    public Task AlphaShowsOverACheckerboardBesideTheOpaqueColor() => EnqueueAsync(async () =>
    {
        var box = new ColorBox { HasAlpha = true, Red = 1, Alpha = 0.5 };
        await LoadTestContentAsync(box).ConfigureAwait(true);

        _ = SwatchColor(box).Should().Be(Color.FromArgb(255, 255, 0, 0));
        _ = ((SolidColorBrush)Part<Border>(box, "PartSwatchAlphaFill").Background).Color.Should().Be(Color.FromArgb(128, 255, 0, 0));
        _ = Grid.GetColumnSpan(Part<Border>(box, "PartSwatchFill")).Should().Be(1);

        box.HasAlpha = false;

        _ = Grid.GetColumnSpan(Part<Border>(box, "PartSwatchFill")).Should().Be(2);
        _ = Part<Grid>(box, "PartSwatchAlpha").Visibility.Should().Be(Visibility.Collapsed);
    });

    [TestMethod]
    [DataRow(LabelPosition.Left, LabelPosition.Left)]
    [DataRow(LabelPosition.Right, LabelPosition.Right)]
    [DataRow(LabelPosition.Top, LabelPosition.Left)]
    public Task ChannelLabelsShowInsideTheirFieldsOnTheChosenSide(LabelPosition requested, LabelPosition shown) => EnqueueAsync(async () =>
    {
        var box = new ColorBox { HasAlpha = true, ChannelLabelPosition = requested };
        await LoadTestContentAsync(box).ConfigureAwait(true);

        _ = ShownChannels(box).Should().OnlyContain(channel => channel.IsCompact && channel.ActualLabelPosition == shown, "channel labels are always compact");
        _ = ShownChannels(box).Select(static channel => channel.Label).Should().Equal("R", "G", "B", "A");
    });

    private static T Part<T>(ColorBox box, string name)
        where T : FrameworkElement
        => box.FindDescendant<T>(element => string.Equals(element.Name, name, StringComparison.Ordinal))!;

    private static Color SwatchColor(ColorBox box) => ((SolidColorBrush)Part<Border>(box, "PartSwatchFill").Background).Color;

    private static NumberBox[] ShownChannels(ColorBox box)
        => [.. box.FindDescendants().OfType<NumberBox>()
            .Where(static channel => channel.FindAscendant<FrameworkElement>() is { Visibility: Visibility.Visible })
            .OrderBy(static channel => channel.Name[^1] switch { 'X' => 0, 'Y' => 1, 'Z' => 2, _ => 3 })];

    private static async Task PickAsync(ColorBox box, Color color)
    {
        var flyout = (Flyout)Part<Button>(box, "PartSwatchButton").Flyout;
        var opened = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        flyout.Opened += (_, _) => opened.TrySetResult();
        flyout.Closed += (_, _) => closed.TrySetResult();
        flyout.ShowAt(Part<Button>(box, "PartSwatchButton"));
        await opened.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
        ((ColorPicker)flyout.Content).Color = color;
        flyout.Hide();
        await closed.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
    }
}
