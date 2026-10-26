// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Expander = Microsoft.UI.Xaml.Controls.Expander;
using NumberBox = DroidNet.Controls.NumberBox;
using static Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors.LayoutHosts;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors;

/// <summary>
///     The responsive layout of a properties section: header actions, header wrapping against the offered width, and
///     the spacing of its items.
/// </summary>
[TestClass]
internal sealed class PropertiesExpanderLayoutTests : VisualUserInterfaceTests
{
    private static readonly double[] HeaderWidths = [480d, 280d];

    [TestMethod]
    public Task HeaderActionsStaySeparateFromDisclosureAndWrapWithTheHeader() => EnqueueAsync(async () =>
    {
        var reset = new Button { Content = "Reset", MinHeight = 32 };
        var section = new PropertiesExpander
        {
            Header = "Sky Atmosphere",
            Description = "A supporting description that wraps without overlapping section actions.",
            HeaderActions = reset,
            IsExpanded = true,
        };
        section.Items.Add(new PropertyCard { PropertyName = "Elevation", Content = new NumberBox { Unit = "°" } });
        var host = CreateHost(280, section);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        _ = reset.ActualWidth.Should().BeGreaterThan(0);
        _ = section.IsTabStop.Should().BeFalse();
        _ = section.FindDescendant<Expander>().Should().NotBeNull();
        _ = section.FindDescendants().OfType<Button>().Should().Contain(reset);
        foreach (var width in HeaderWidths)
        {
            host.Width = width;
            host.UpdateLayout();
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            var description = section.FindDescendant<ContentPresenter>(item => string.Equals(item.Name, "PartDescriptionPresenter", StringComparison.Ordinal))!;
            var descriptionPoint = description.TransformToVisual(section).TransformPoint(default);
            var actionPoint = reset.TransformToVisual(section).TransformPoint(default);
            _ = (descriptionPoint.X + description.ActualWidth).Should().BeLessThanOrEqualTo(actionPoint.X + 1);
            _ = (actionPoint.X + reset.ActualWidth).Should().BeLessThanOrEqualTo(section.ActualWidth + 1);
            _ = section.IsExpanded.Should().BeTrue();
        }
    });

    [TestMethod]
    public Task SectionHeadersFollowTheOfferedWidthNotTheirOwn() => EnqueueAsync(async () =>
    {
        // A host that sizes the section to its content offers more width than the section ends up with.
        var reset = new Button { Content = "Reset" };
        var section = new PropertiesExpander { Header = "Camera", HeaderIcon = new FontIcon { Glyph = "" }, Content = reset };
        var host = CreateHost(410, new ContentPresenter { HorizontalContentAlignment = HorizontalAlignment.Left, Content = section });
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var content = section.FindDescendant<ContentPresenter>(part => string.Equals(part.Name, "PartContentPresenter", StringComparison.Ordinal))!;
        var title = section.FindDescendant<StackPanel>(part => string.Equals(part.Name, "HeaderPanel", StringComparison.Ordinal))!;
        var icon = section.FindDescendant<Viewbox>(part => string.Equals(part.Name, "PartHeaderIconPresenterHolder", StringComparison.Ordinal))!;
        _ = section.ActualWidth.Should().BeLessThan(section.HeaderWrapThreshold, "the host sizes the section to its content");

        _ = Grid.GetRow(content).Should().Be(0, "the offered width, not the section's own, keeps the content beside the title");
        _ = content.Visibility.Should().Be(Visibility.Visible);
        _ = title.Margin.Right.Should().Be(12, "content beside the title keeps its gap");
        _ = icon.Visibility.Should().Be(Visibility.Visible);

        host.Width = 320;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = Grid.GetRow(content).Should().Be(1, "a narrow offer moves the content under the title");
        _ = title.Margin.Right.Should().Be(0);
        _ = icon.Visibility.Should().Be(Visibility.Visible);

        host.Width = 280;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = icon.Visibility.Should().Be(Visibility.Collapsed, "a very narrow header gives the title the icon's room");

        section.Content = null;
        host.Width = 760;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = content.Visibility.Should().Be(Visibility.Collapsed);
        _ = title.Margin.Right.Should().Be(0, "without content beside it, the title keeps no gap");
    });

    [TestMethod]
    public Task CollapsedSectionItemsLeaveNoGap() => EnqueueAsync(async () =>
    {
        var first = new PropertyCard { PropertyName = "Near plane", Content = new NumberBox { Unit = "m" } };
        var hidden = new PropertyCard { PropertyName = "Hidden", Content = new NumberBox(), Visibility = Visibility.Collapsed };
        var last = new PropertyCard { PropertyName = "Far plane", Content = new NumberBox { Unit = "m" } };
        var section = new PropertiesExpander { Header = "Camera", IsExpanded = true };
        section.Items.Add(first);
        section.Items.Add(hidden);
        section.Items.Add(last);
        var host = CreateHost(480, section);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var spacing = section.FindDescendant<ItemsControl>()!.FindDescendant<StackPanel>()!.Spacing;

        var firstBottom = first.TransformToVisual(section).TransformPoint(default).Y + first.ActualHeight;
        var lastTop = last.TransformToVisual(section).TransformPoint(default).Y;
        _ = (lastTop - firstBottom).Should().BeApproximately(spacing, 1, "one gap separates the visible rows around a collapsed one");
    });
}
