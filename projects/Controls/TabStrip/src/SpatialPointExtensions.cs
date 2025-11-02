// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

/// <summary>
///     Extension methods for converting <see cref="SpatialPoint"/> between coordinate spaces.
/// </summary>
public static class SpatialPointExtensions
{
    /// <summary>
    ///     Converts a <see cref="SpatialPoint"/> to a different coordinate space.
    /// </summary>
    /// <param name="source">The source spatial point.</param>
    /// <param name="targetSpace">The target coordinate space.</param>
    /// <param name="targetElement">The target UIElement when converting to CoordinateSpace.Element.</param>
    /// <returns>A new SpatialPoint in the target coordinate space.</returns>
    /// <exception cref="ArgumentException">Thrown when conversion requirements are not met.</exception>
    public static SpatialPoint To(this SpatialPoint source, CoordinateSpace targetSpace, UIElement? targetElement = null)
    {
        // Already in target space
        if (source.Space == targetSpace &&
            (targetSpace != CoordinateSpace.Element || ReferenceEquals(source.ReferenceElement, targetElement)))
        {
            return source;
        }

        // Validate target element requirement
        if (targetSpace == CoordinateSpace.Element && targetElement is null)
        {
            throw new ArgumentNullException(nameof(targetElement), "Element coordinate space requires a target element");
        }

        // Convert to intermediate Window space if needed
        var windowPoint = source.Space switch
        {
            CoordinateSpace.Element => ConvertElementToWindow(source.Point, source.ReferenceElement!),
            CoordinateSpace.Window => source.Point,
            CoordinateSpace.Screen => ConvertScreenToWindow(source.Point, source.ReferenceElement ?? targetElement),
            _ => throw new ArgumentOutOfRangeException(null, "Invalid source coordinate space"),
        };

        // Convert from Window to target space
        var targetPoint = targetSpace switch
        {
            CoordinateSpace.Element => ConvertWindowToElement(windowPoint, targetElement!),
            CoordinateSpace.Window => windowPoint,
            CoordinateSpace.Screen => ConvertWindowToScreen(windowPoint, source.ReferenceElement ?? targetElement),
            _ => throw new ArgumentOutOfRangeException(null, "Invalid target coordinate space"),
        };

        return new SpatialPoint(targetPoint, targetSpace, targetElement);
    }

    private static Windows.Foundation.Point ConvertElementToWindow(Windows.Foundation.Point point, UIElement element)
    {
        var transform = element.TransformToVisual(null);
        return transform.TransformPoint(point);
    }

    private static Windows.Foundation.Point ConvertWindowToElement(Windows.Foundation.Point point, UIElement element)
    {
        var transform = element.TransformToVisual(null);
        var inverse = transform.Inverse;
        return inverse.TransformPoint(point);
    }

    private static Windows.Foundation.Point ConvertScreenToWindow(Windows.Foundation.Point screenPoint, UIElement? referenceElement)
    {
        if (referenceElement is null)
        {
            throw new InvalidOperationException("Cannot convert Screen to Window without a reference element");
        }

        // Get the window's screen position using GetPhysicalScreenBoundsUsingWindowRect
        var windowBounds = Native.GetPhysicalScreenBoundsUsingWindowRect((Microsoft.UI.Xaml.FrameworkElement)referenceElement);
        if (!windowBounds.HasValue)
        {
            throw new InvalidOperationException("Failed to get window bounds");
        }

        // Convert screen to window: subtract window's top-left
        return new Windows.Foundation.Point(
            screenPoint.X - windowBounds.Value.Left,
            screenPoint.Y - windowBounds.Value.Top);
    }

    private static Windows.Foundation.Point ConvertWindowToScreen(Windows.Foundation.Point windowPoint, UIElement? referenceElement)
    {
        if (referenceElement is null)
        {
            throw new InvalidOperationException("Cannot convert Window to Screen without a reference element");
        }

        // Get the window's screen position
        var windowBounds = Native.GetPhysicalScreenBoundsUsingWindowRect((Microsoft.UI.Xaml.FrameworkElement)referenceElement);
        if (!windowBounds.HasValue)
        {
            throw new InvalidOperationException("Failed to get window bounds");
        }

        // Convert window to screen: add window's top-left
        return new Windows.Foundation.Point(
            windowPoint.X + windowBounds.Value.Left,
            windowPoint.Y + windowBounds.Value.Top);
    }
}
