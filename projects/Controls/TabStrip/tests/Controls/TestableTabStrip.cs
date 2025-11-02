// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Tabs.Tests;

/// <summary>
///     A testable subclass of <see cref="TabStrip" /> that exposes protected members for testing.
/// </summary>
public sealed partial class TestableTabStrip : TabStrip
{
    public new TabStripItem? DraggedItem => base.DraggedItem;

    public new void HandlePointerPressed(TabStripItem? hitItem, Windows.Foundation.Point position)
        => base.HandlePointerPressed(hitItem, position);

    public new bool HandlePointerMoved(Windows.Foundation.Point currentPoint)
        => base.HandlePointerMoved(currentPoint);

    public new bool HandlePointerReleased(Windows.Foundation.Point screenPoint)
        => base.HandlePointerReleased(screenPoint);

    public new void BeginDrag(TabStripItem item, Windows.Foundation.Point? initialScreenPoint = null)
        => base.BeginDrag(item, initialScreenPoint);

    public new ItemsRepeater? GetRegularItemsRepeater()
        => base.GetRegularItemsRepeater();

    public new Windows.Foundation.Point ScreenToStrip(Windows.Foundation.Point physicalScreenPoint)
        => base.ScreenToStrip(physicalScreenPoint);

    public new Windows.Foundation.Point StripToScreen(Windows.Foundation.Point stripPoint)
        => base.StripToScreen(stripPoint);

    /// <summary>
    ///     Simulates a drag move by calling the coordinator with strip-relative coordinates.
    ///     This allows tests to work entirely in logical TabStrip coordinates without
    ///     dealing with screen coordinate conversion.
    /// </summary>
    /// <param name="stripPoint">The position in TabStrip-relative logical coordinates.</param>
    public void SimulateDragMove(Windows.Foundation.Point stripPoint)
    {
        if (this.DragCoordinator is not null)
        {
            // Create SpatialPoint in Element space (relative to this TabStrip)
            var spatialPoint = new SpatialPoint(stripPoint, CoordinateSpace.Element, this);

            // Convert to Screen space for the coordinator
            var screenPoint = spatialPoint.To(CoordinateSpace.Screen, this);

            this.DragCoordinator.UpdateDragPosition(screenPoint.Point);
        }
    }
}
