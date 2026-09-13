// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Controls.ToolBars.Tests;

/// <summary>Verifies that overflow uses only the space required by the visible toolbar.</summary>
[TestClass]
[TestCategory("UITest")]
public sealed class ToolBarOverflowTests : VisualUserInterfaceTests
{
    /// <summary>Overflow changes containers without overwriting a command's visibility binding or exposing hidden menu entries.</summary>
    /// <param name="secondary">Whether the conditional command belongs to the secondary group.</param>
    /// <returns>The asynchronous visibility and resize regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task HiddenCommandsStayHiddenAcrossOverflowAndBindingChanges(bool secondary) => EnqueueAsync(async () =>
    {
        var toolbar = new ToolBar { Width = 240, Padding = new Thickness(0) };
        toolbar.PrimaryItems.Add(new ToolBarButton { Label = "One", Width = 80 });
        toolbar.PrimaryItems.Add(new ToolBarButton { Label = "Two", Width = 80 });
        var source = new Border { Visibility = Visibility.Collapsed };
        var conditional = new ToolBarButton { Label = "Conditional", Width = 80 };
        conditional.SetBinding(UIElement.VisibilityProperty, new Binding { Source = source, Path = new PropertyPath(nameof(UIElement.Visibility)), Mode = BindingMode.OneWay });
        var group = secondary ? toolbar.SecondaryItems : toolbar.PrimaryItems;
        group.Add(conditional);
        await LoadTestContentAsync(toolbar).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var items = GetPartOrFail<ItemsControl>(toolbar, secondary ? ToolBar.SecondaryItemsControlPartName : ToolBar.PrimaryItemsControlPartName);
        var container = (FrameworkElement)items.ContainerFromItem(conditional);
        _ = container.Should().NotBeSameAs(conditional);
        _ = conditional.Visibility.Should().Be(Visibility.Collapsed);
        _ = container.Visibility.Should().Be(Visibility.Collapsed);
        _ = toolbar.OverflowButtonVisibility.Should().Be(Visibility.Collapsed);

        toolbar.Width = 60;
        await WaitForRenderAsync().ConfigureAwait(true);
        var overflow = (MenuFlyout)GetPartOrFail<Button>(toolbar, ToolBar.OverflowButtonPartName).Flyout;
        _ = overflow.Items.OfType<MenuFlyoutItem>().Select(item => item.Text).Should().NotContain("Conditional");
        _ = conditional.Visibility.Should().Be(Visibility.Collapsed);

        source.Visibility = Visibility.Visible;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = conditional.Visibility.Should().Be(Visibility.Visible);
        _ = overflow.Items.OfType<MenuFlyoutItem>().Select(item => item.Text).Should().Contain("Conditional");

        source.Visibility = Visibility.Collapsed;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = overflow.Items.OfType<MenuFlyoutItem>().Select(item => item.Text).Should().NotContain("Conditional");
        toolbar.Width = 360;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = conditional.Visibility.Should().Be(Visibility.Collapsed);
        _ = container.Visibility.Should().Be(Visibility.Collapsed);
        source.Visibility = Visibility.Visible;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = container.Visibility.Should().Be(Visibility.Visible);
        _ = conditional.ActualWidth.Should().BePositive();
        _ = toolbar.OverflowButtonVisibility.Should().Be(Visibility.Collapsed);
    });

    /// <summary>A compact icon in an Auto column stays visible beside a selection summary.</summary>
    /// <returns>The asynchronous toolbar layout regression.</returns>
    [TestMethod]
    public Task AutoSizedIconToolbarKeepsItsOnlyButtonVisible() => EnqueueAsync(async () =>
    {
        var grid = new Grid { Width = 400 };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.Children.Add(new TextBlock { Text = "2 objects" });
        var toolbar = new ToolBar { IsCompact = true };
        var all = new ToolBarToggleButton { Label = "All components", ToolBarLabelPosition = ToolBarLabelPosition.Collapsed, Icon = new SymbolIconSource { Symbol = Symbol.AllApps } };
        toolbar.PrimaryItems.Add(all);
        Grid.SetColumn(toolbar, 1);
        grid.Children.Add(toolbar);
        await LoadTestContentAsync(grid).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = toolbar.OverflowButtonVisibility.Should().Be(Visibility.Collapsed);
        var items = GetPartOrFail<ItemsControl>(toolbar, ToolBar.PrimaryItemsControlPartName);
        _ = ((FrameworkElement)items.ContainerFromIndex(0)).Visibility.Should().Be(Visibility.Visible);
        _ = all.ActualWidth.Should().BePositive();
    });

    /// <summary>When both groups fit exactly, neither requires an overflow menu.</summary>
    /// <returns>The asynchronous width regression.</returns>
    [TestMethod]
    public Task PrimaryAndSecondaryItemsUseAvailableSpaceBeforeOverflow() => EnqueueAsync(async () =>
    {
        var toolbar = new ToolBar { Width = 300, Padding = new Thickness(0) };
        toolbar.PrimaryItems.Add(new ToolBarButton { Label = "One", Width = 50 });
        toolbar.SecondaryItems.Add(new ToolBarButton { Label = "Two", Width = 50 });
        await LoadTestContentAsync(toolbar).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var primary = GetPartOrFail<ItemsControl>(toolbar, ToolBar.PrimaryItemsControlPartName);
        var secondary = GetPartOrFail<ItemsControl>(toolbar, ToolBar.SecondaryItemsControlPartName);
        var scale = toolbar.XamlRoot.RasterizationScale;
        toolbar.Width = Math.Ceiling((primary.DesiredSize.Width + secondary.DesiredSize.Width) * scale) / scale;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = toolbar.OverflowButtonVisibility.Should().Be(Visibility.Collapsed);
        _ = secondary.Visibility.Should().Be(Visibility.Visible);
    });

    /// <summary>Real overflow clears when the toolbar receives sufficient width again.</summary>
    /// <returns>The asynchronous resize regression.</returns>
    [TestMethod]
    public Task WideningToolbarRestoresOverflowedItems() => EnqueueAsync(async () =>
    {
        var toolbar = new ToolBar { Width = 60, Padding = new Thickness(0) };
        toolbar.PrimaryItems.Add(new ToolBarButton { Label = "One", Width = 50 });
        toolbar.PrimaryItems.Add(new ToolBarButton { Label = "Two", Width = 50 });
        await LoadTestContentAsync(toolbar).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = toolbar.OverflowButtonVisibility.Should().Be(Visibility.Visible);
        toolbar.Width = 160;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = toolbar.OverflowButtonVisibility.Should().Be(Visibility.Collapsed);
        var items = GetPartOrFail<ItemsControl>(toolbar, ToolBar.PrimaryItemsControlPartName);
        _ = ((FrameworkElement)items.ContainerFromIndex(0)).Visibility.Should().Be(Visibility.Visible);
        _ = ((FrameworkElement)items.ContainerFromIndex(1)).Visibility.Should().Be(Visibility.Visible);
    });

    private static T GetPartOrFail<T>(ToolBar toolbar, string name)
        where T : FrameworkElement
    {
        var part = toolbar.FindDescendant<T>(element => string.Equals(element.Name, name, StringComparison.Ordinal));
        _ = part.Should().NotBeNull();
        return part!;
    }
}
