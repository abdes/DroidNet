// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace DroidNet.Controls.Tests;

/// <summary>Exercises two, three and four component vectors, including dimension changes at runtime.</summary>
[TestClass]
[TestCategory("UITest")]
public sealed class VectorBoxDimensionTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task FourComponentsShowEveryValueAndLabelW() => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { Dimension = 4, ComponentLabelPosition = LabelPosition.Left, XValue = 1, YValue = 2, ZValue = 3, WValue = 4 };
        vector.ComponentLabels[Component.W] = "A";
        await LoadTestContentAsync(vector).ConfigureAwait(true);

        _ = ShownEditors(vector).Select(static box => (box.Label, box.NumberValue)).Should().Equal(("X", 1d), ("Y", 2d), ("Z", 3d), ("A", 4d));
        _ = vector.GetValues().Should().Equal(1, 2, 3, 4);
    });

    [TestMethod]
    public Task ChangingTheDimensionShowsAndHidesComponentsAndKeepsTheirValues() => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { Dimension = 2, ComponentLabelPosition = LabelPosition.Left, XValue = 1, YValue = 2, ZValue = 3, WValue = 4 };
        await LoadTestContentAsync(vector).ConfigureAwait(true);
        _ = ShownEditors(vector).Should().HaveCount(2);

        vector.Dimension = 4;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

        _ = ShownEditors(vector).Select(static box => box.NumberValue).Should().Equal(1, 2, 3, 4);

        vector.Dimension = 3;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

        _ = ShownEditors(vector).Should().HaveCount(3);
        _ = vector.WValue.Should().Be(4, "hidden components keep their values");
        _ = vector.GetValues().Should().Equal(1, 2, 3);
    });

    [TestMethod]
    public Task SettingWValidatesItAndUpdatesTheAggregate() => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { Dimension = 4 };
        var targets = new List<object?>();
        vector.Validate += (_, args) =>
        {
            targets.Add(args.Target);
            args.IsValid = args.NewValue <= 1;
        };
        await LoadTestContentAsync(vector).ConfigureAwait(true);
        targets.Clear();

        vector.WValue = 0.5;
        vector.WValue = 2;

        _ = targets.Should().NotBeEmpty().And.OnlyContain(static target => Equals(target, Component.W));
        _ = vector.WValue.Should().Be(0.5, "the rejected value is reverted");
    });

    [TestMethod]
    public Task SetValuesTakesOneValuePerShownComponent() => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { Dimension = 4, WIsIndeterminate = true };
        await LoadTestContentAsync(vector).ConfigureAwait(true);

        vector.SetValues([1, 2, 3, 4]);
        var wrongCount = () => vector.SetValues([1, 2, 3]);

        _ = vector.GetValues().Should().Equal(1, 2, 3, 4);
        _ = vector.WIsIndeterminate.Should().BeFalse();
        _ = wrongCount.Should().Throw<ArgumentException>();
    });

    [TestMethod]
    public Task ComponentSettingsChangedAfterLoadingApplyAtOnce() => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { ComponentLabelPosition = LabelPosition.Left, XValue = 1.25 };
        await LoadTestContentAsync(vector).ConfigureAwait(true);
        var x = ShownEditors(vector)[0];

        vector.ComponentLabels[Component.X] = "U";
        vector.ComponentMasks[Component.X] = "~.# m";

        _ = x.Label.Should().Be("U");
        _ = x.Mask.Should().Be("~.# m");

        _ = vector.ComponentMasks.Remove(Component.X);

        _ = x.Mask.Should().Be(vector.ComponentMask);
    });

    [TestMethod]
    [DataRow(2)]
    [DataRow(3)]
    [DataRow(4)]
    public Task UnlimitedMeasureAsksForOneRowOfUsableComponents(int dimension) => EnqueueAsync(async () =>
    {
        var vector = new VectorBox { Dimension = dimension, AutoStackComponents = true, Padding = new Thickness(0) };
        await LoadTestContentAsync(vector).ConfigureAwait(true);

        vector.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        var rowWidth = vector.DesiredSize.Width;

        _ = rowWidth.Should().BeGreaterThanOrEqualTo(vector.ComponentMinimumWidth * dimension, "every component keeps its usable width on one row");

        vector.Measure(new Size(rowWidth, double.PositiveInfinity));
        vector.Arrange(new Rect(0, 0, rowWidth, vector.DesiredSize.Height));
        _ = ShownEditors(vector).Select(static box => Grid.GetRow((FrameworkElement)box.Parent)).Should().AllBeEquivalentTo(0, "the reported width fits one row");

        vector.Measure(new Size(rowWidth - 1, double.PositiveInfinity));
        vector.Arrange(new Rect(0, 0, rowWidth - 1, vector.DesiredSize.Height));
        _ = ShownEditors(vector).Select(static box => Grid.GetRow((FrameworkElement)box.Parent)).Should().Equal(Enumerable.Range(0, dimension), "anything narrower stacks");
    });

    private static NumberBox[] ShownEditors(VectorBox vector)
        => [.. vector.FindDescendants().OfType<NumberBox>()
            .Where(static box => box.Name.StartsWith("PartNumberBox", StringComparison.Ordinal)
                && box.FindAscendant<FrameworkElement>() is { Visibility: Visibility.Visible })
            .OrderBy(static box => box.Name[^1] switch { 'X' => 0, 'Y' => 1, 'Z' => 2, _ => 3 })];
}
