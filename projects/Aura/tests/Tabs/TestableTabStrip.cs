// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.WinUI;
using DroidNet.Coordinates;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;

namespace DroidNet.Aura.Controls.Tests;

/// <summary>
///     A testable subclass of <see cref="TabStrip"/> that exposes protected members for testing.
/// </summary>
[ExcludeFromCodeCoverage]
public sealed partial class TestableTabStrip : TabStrip
{
    /// <summary>
    ///     Gets or sets the layout manager, exposing the protected property for test injection.
    /// </summary>
    public new TabStripLayoutManager LayoutManager
    {
        get => base.LayoutManager;
        set => base.LayoutManager = value;
    }

    /// <summary>
    ///     Gets a value indicating whether a drag operation is engaged (pointer pressed on
    ///     draggable item).
    /// </summary>
    public new bool IsDragEngaged => base.IsDragEngaged;

    /// <summary>
    ///     Gets a value indicating whether a drag operation has been initiated and is ongoing.
    /// </summary>
    public new bool IsDragOngoing => base.IsDragOngoing;

    /// <summary>
    ///     Exposes HandlePointerPressed for direct testing without PointerRoutedEventArgs.
    /// </summary>
    /// <param name="hitItem">The tab strip item that was hit by the pointer, or null.</param>
    /// <param name="position">The position of the pointer in screen space.</param>
    /// <param name="hotspotOffsets">The offset of the pointer hotspot relative to the tab item, used for drag operations.</param>
    public new void HandlePointerPressed(TabStripItem hitItem, SpatialPoint<ElementSpace> position, Point hotspotOffsets)
        => base.HandlePointerPressed(hitItem, position, hotspotOffsets);

    /// <summary>
    ///     Exposes HandlePointerMoved for direct testing without PointerRoutedEventArgs.
    /// </summary>
    /// <param name="currentPoint">The current pointer position in element space.</param>
    public new bool HandlePointerMoved(SpatialPoint<ElementSpace> currentPoint)
        => base.HandlePointerMoved(currentPoint);

    public void MovePointer(SpatialPoint<ElementSpace> currentPoint, double deltaX, double deltaY)
        => _ = this.HandlePointerMoved(currentPoint + new Point(deltaX, deltaY).AsElement());

    /// <summary>
    ///     Exposes HandlePointerReleased for direct testing without PointerRoutedEventArgs.
    /// </summary>
    /// <param name="screenPoint">The pointer position in screen space.</param>
    public new bool HandlePointerReleased(SpatialPoint<ScreenSpace> screenPoint)
        => base.HandlePointerReleased(screenPoint);

    /// <summary>
    /// Gets the root grid template part.
    /// </summary>
    public Grid? GetRootGrid()
        => this.FindDescendant<Grid>(e => string.Equals(e.Name, RootGridPartName, StringComparison.Ordinal));

    /// <summary>
    ///     Gets the pinned items repeater template part.
    /// </summary>
    public ItemsRepeater? GetPinnedRepeater()
        => this.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, PartPinnedItemsRepeaterName, StringComparison.Ordinal));

    /// <summary>
    /// Gets the regular items repeater template part.
    /// </summary>
    public ItemsRepeater? GetRegularRepeater()
        => this.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, PartRegularItemsRepeaterName, StringComparison.Ordinal));

    /// <summary>
    ///     Gets the scroll host template part.
    /// </summary>
    public ScrollViewer? GetScrollHost()
        => this.FindDescendant<ScrollViewer>(e => string.Equals(e.Name, PartScrollHostName, StringComparison.Ordinal));

    /// <summary>
    ///     Returns the first descendant <see cref="TabStripItem"/> contained inside the element
    ///     prepared for the item at <paramref name="index"/> (the visual wrapper). Tests that need
    ///     to access the visual TabStripItem should use this helper rather than casting the outer
    ///     container directly.
    /// </summary>
    /// <param name="index">Index of the logical item.</param>
    public TabStripItem? GetTabStripItemForIndex(int index)
        => this.GetContainerForIndex(index) as TabStripItem;

    /// <summary>
    /// Gets the left overflow button template part.
    /// </summary>
    public RepeatButton? GetOverflowLeftButton()
        => this.FindDescendant<RepeatButton>(e => string.Equals(e.Name, PartOverflowLeftButtonName, StringComparison.Ordinal));

    /// <summary>
    ///     Gets the right overflow button template part.
    /// </summary>
    public RepeatButton? GetOverflowRightButton()
        => this.FindDescendant<RepeatButton>(e => string.Equals(e.Name, PartOverflowRightButtonName, StringComparison.Ordinal));

    /// <summary>
    ///     Simulates clicking the left overflow button by calling the protected handler.
    /// </summary>
    public new void HandleOverflowLeftClick()
        => base.HandleOverflowLeftClick();

    /// <summary>
    ///     Simulates clicking the right overflow button by calling the protected handler.
    /// </summary>
    public new void HandleOverflowRightClick()
        => base.HandleOverflowRightClick();

    /// <summary>
    ///     Simulates a tab close request by calling the protected handler.
    /// </summary>
    /// <param name="item">The tab item to close.</param>
    public void HandleTabCloseRequest(TabItem item)
        => this.HandleTabCloseRequest(this, new TabCloseRequestedEventArgs { Item = item });

    /// <summary>
    ///     Simulates a tab being tapped by calling the protected OnTabElementTapped handler.
    /// </summary>
    /// <param name="tabStripItem">The tab strip item that was tapped.</param>
    public void SimulateTap(TabStripItem tabStripItem)
    {
        var args = new TappedRoutedEventArgs();
        this.OnTabElementTapped(tabStripItem, args);
    }
}
