// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Provides layout logic for positioning popup content relative to an anchor, viewport, and pointer context.
/// </summary>
/// <remarks>
///     From the viewer's perspective the popup prefers to stay anchored to its parent. When there is enough room,
///     the content sits beside the anchor while staying inside the window bounds. If the available space shrinks,
///     the popup hugs the edges that keep it visible, and once space returns it realigns with the anchor without a
///     noticeable jump. Throughout these adjustments the item that was under the pointer before the move stays under
///     the pointer after the move so hover feedback does not flicker.
/// </remarks>
internal sealed class PopupPlacementHelper
{
    private const double WindowEdgePadding = 4d;
    private const double PointerEdgePadding = 1.5d;
    private const double PointerSafetyMargin = 12d;

    private PlacementState state = PlacementState.Empty;
    private Point? pointerPosition;
    private Rect? pointerItemBounds;

    private enum HorizontalAttachment
    {
        None,
        AnchorLeading,
        AnchorTrailing,
        WindowLeading,
        WindowTrailing,
        CustomOrigin,
    }

    private enum VerticalAttachment
    {
        None,
        AnchorTop,
        AnchorBottom,
        WindowTop,
        WindowBottom,
        CustomOrigin,
    }

    /// <summary>
    ///     Updates the tracked pointer position to help maintain pointer safety when the popup repositions.
    /// </summary>
    /// <param name="pointer">Pointer coordinates in window space.</param>
    /// <param name="hoveredItemBounds">Optional bounds of the currently hovered menu item.</param>
    public void UpdatePointer(Point pointer, Rect? hoveredItemBounds = null)
    {
        this.pointerPosition = pointer;
        this.pointerItemBounds = hoveredItemBounds;
    }

    /// <summary>
    ///     Clears cached placement state and pointer tracking information.
    /// </summary>
    public void Reset()
    {
        this.state = PlacementState.Empty;
        this.pointerPosition = null;
        this.pointerItemBounds = null;
    }

    /// <summary>
    ///     Attempts to resolve an offset for the popup content that honors anchor, viewport, and pointer constraints.
    /// </summary>
    /// <param name="request">Placement request describing the current anchor and content.</param>
    /// <param name="result">Receives the final placement information when the method returns <see langword="true"/>.</param>
    /// <returns><see langword="true"/> when placement succeeded; otherwise <see langword="false"/>.</returns>
    public bool TryPlace(PlacementRequest request, out PlacementResult result)
    {
        result = default;

        if (request.Anchor is null || request.Content is null)
        {
            return false;
        }

        if (!TryResolveAnchorAndSize(request, out var anchorBounds, out var contentSize))
        {
            return false;
        }

        this.EnsureStateForToken(request.Token);

        var anchorHorizontalMin = anchorBounds.Right - contentSize.Width;
        var anchorHorizontalMax = request.CustomPosition.HasValue ? anchorBounds.Left : anchorBounds.Right;
        var anchorVerticalMin = anchorBounds.Bottom - contentSize.Height;
        var anchorVerticalMax = request.CustomPosition.HasValue ? anchorBounds.Top : anchorBounds.Bottom;

        // Start from the anchor-derived ranges, then tighten them with viewport and pointer constraints.
        var ranges = InitializePlacementRanges(anchorHorizontalMin, anchorHorizontalMax, anchorVerticalMin, anchorVerticalMax);
        var constraints = ApplyViewportConstraints(request.Viewport, contentSize, ref ranges);

        var offset = GetPreferredOffset(anchorBounds, request.CustomPosition, ranges);

        offset = this.ApplyStateAffinity(
            anchorBounds,
            contentSize,
            anchorVerticalMin,
            anchorVerticalMax,
            ref ranges,
            constraints,
            offset);

        var attachments = DetermineAttachments(
            request.CustomPosition,
            anchorBounds,
            contentSize,
            anchorVerticalMin,
            anchorVerticalMax,
            constraints,
            offset);

        this.state = new PlacementState(request.Token, offset, contentSize, attachments.Horizontal, attachments.Vertical);
        result = new PlacementResult(offset, contentSize, anchorBounds);
        return true;
    }

    private static PlacementRanges InitializePlacementRanges(double horizontalMin, double horizontalMax, double verticalMin, double verticalMax) =>
        new(horizontalMin, horizontalMax, verticalMin, verticalMax);

    private static ViewportConstraints ApplyViewportConstraints(Rect viewport, Size contentSize, ref PlacementRanges ranges)
    {
        var horizontalApplied = false;
        var verticalApplied = false;
        var windowHorizontalMin = double.NaN;
        var windowHorizontalMax = double.NaN;
        var windowVerticalMin = double.NaN;
        var windowVerticalMax = double.NaN;

        if (IsPositiveFinite(viewport.Width))
        {
            var availableWidth = Math.Max(0d, viewport.Width - (2 * WindowEdgePadding));
            if (contentSize.Width <= availableWidth)
            {
                windowHorizontalMin = WindowEdgePadding;
                windowHorizontalMax = viewport.Width - WindowEdgePadding - contentSize.Width;
                ranges.IntersectHorizontal(windowHorizontalMin, windowHorizontalMax);
                horizontalApplied = true;
            }
        }

        if (IsPositiveFinite(viewport.Height))
        {
            var availableHeight = Math.Max(0d, viewport.Height - (2 * WindowEdgePadding));
            if (contentSize.Height <= availableHeight)
            {
                windowVerticalMin = WindowEdgePadding;
                windowVerticalMax = viewport.Height - WindowEdgePadding - contentSize.Height;
                ranges.IntersectVertical(windowVerticalMin, windowVerticalMax);
                verticalApplied = true;
            }
        }

        return new ViewportConstraints(horizontalApplied, windowHorizontalMin, windowHorizontalMax, verticalApplied, windowVerticalMin, windowVerticalMax);
    }

    private static AttachmentResult DetermineAttachments(
        Point? customPosition,
        Rect anchorBounds,
        Size contentSize,
        double anchorVerticalMin,
        double anchorVerticalMax,
        ViewportConstraints constraints,
        Point offset)
    {
        var horizontalAttachment = HorizontalAttachment.None;
        if (constraints.HorizontalApplied && AreClose(offset.X, constraints.HorizontalMin))
        {
            horizontalAttachment = HorizontalAttachment.WindowLeading;
        }
        else if (constraints.HorizontalApplied && AreClose(offset.X, constraints.HorizontalMax))
        {
            horizontalAttachment = HorizontalAttachment.WindowTrailing;
        }
        else if (customPosition.HasValue)
        {
            horizontalAttachment = HorizontalAttachment.CustomOrigin;
        }
        else if (AreClose(offset.X, anchorBounds.Left))
        {
            horizontalAttachment = HorizontalAttachment.AnchorLeading;
        }
        else if (AreClose(offset.X + contentSize.Width, anchorBounds.Right))
        {
            horizontalAttachment = HorizontalAttachment.AnchorTrailing;
        }

        var verticalAttachment = VerticalAttachment.None;
        if (constraints.VerticalApplied && AreClose(offset.Y, constraints.VerticalMin))
        {
            verticalAttachment = VerticalAttachment.WindowTop;
        }
        else if (constraints.VerticalApplied && AreClose(offset.Y, constraints.VerticalMax))
        {
            verticalAttachment = VerticalAttachment.WindowBottom;
        }
        else if (customPosition.HasValue)
        {
            verticalAttachment = VerticalPlacementForCustom(anchorBounds, offset.Y, anchorVerticalMin, anchorVerticalMax);
        }
        else if (AreClose(offset.Y, anchorVerticalMin))
        {
            verticalAttachment = VerticalAttachment.AnchorTop;
        }
        else if (AreClose(offset.Y, anchorVerticalMax))
        {
            verticalAttachment = VerticalAttachment.AnchorBottom;
        }

        return new AttachmentResult(horizontalAttachment, verticalAttachment);
    }

    private static VerticalAttachment VerticalPlacementForCustom(Rect anchorBounds, double vertical, double rangeMin, double rangeMax)
    {
        if (AreClose(vertical, anchorBounds.Top))
        {
            return VerticalAttachment.CustomOrigin;
        }

        if (AreClose(vertical, rangeMin))
        {
            return VerticalAttachment.AnchorTop;
        }

        if (AreClose(vertical, rangeMax))
        {
            return VerticalAttachment.AnchorBottom;
        }

        return VerticalAttachment.CustomOrigin;
    }

    private static bool TryGetAnchorBounds(FrameworkElement anchor, Point? customPosition, out Rect targetBounds)
    {
        try
        {
            var transform = anchor.TransformToVisual(null);
            if (customPosition is { } custom)
            {
                var topLeft = transform.TransformPoint(custom);
                targetBounds = new Rect(topLeft, new Size(1, 1));
            }
            else
            {
                var topLeft = transform.TransformPoint(new Point(0, 0));
                var bottomRight = transform.TransformPoint(new Point(anchor.ActualWidth, anchor.ActualHeight));
                targetBounds = new Rect(topLeft, bottomRight);
            }

            return true;
        }
        catch (InvalidOperationException)
        {
        }
        catch (ArgumentException)
        {
        }

        targetBounds = default;
        return false;
    }

    private static Size MeasureContent(UIElement content, Rect anchorBounds)
    {
        content.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        var desired = content.DesiredSize;
        var actualWidth = content is FrameworkElement fe ? fe.ActualWidth : double.NaN;
        var actualHeight = content is FrameworkElement feElement ? feElement.ActualHeight : double.NaN;

        var width = ResolveDimension(desired.Width, actualWidth, anchorBounds.Width);
        var height = ResolveDimension(desired.Height, actualHeight, anchorBounds.Height);
        return new Size(width, height);
    }

    private static double ResolveDimension(double desired, double actual, double fallback)
    {
        if (double.IsNaN(desired) || double.IsInfinity(desired) || desired <= 0)
        {
            desired = actual;
        }

        if (double.IsNaN(desired) || double.IsInfinity(desired) || desired <= 0)
        {
            desired = fallback;
        }

        return desired > 0 ? desired : 1d;
    }

    private static bool IsPositiveFinite(double value) => !double.IsNaN(value) && !double.IsInfinity(value) && value > 0;

    private static bool AreClose(double first, double second)
    {
        const double tolerance = 0.5;
        return Math.Abs(first - second) <= tolerance;
    }

    private static double SafeClamp(double value, double min, double max)
    {
        if (min > max)
        {
            (min, max) = (max, min);
        }

        return Math.Clamp(value, min, max);
    }

    private static Point GetPreferredOffset(Rect anchorBounds, Point? customPosition, PlacementRanges ranges)
    {
        var preferredHorizontal = anchorBounds.Left;
        var preferredVertical = customPosition.HasValue ? anchorBounds.Top : anchorBounds.Bottom;

        var horizontal = ranges.ClampHorizontal(preferredHorizontal);
        var vertical = ranges.ClampVertical(preferredVertical);

        return new Point(horizontal, vertical);
    }

    private static bool TryResolveAnchorAndSize(PlacementRequest request, out Rect anchorBounds, out Size contentSize)
    {
        anchorBounds = default;
        contentSize = default;

        if (!TryGetAnchorBounds(request.Anchor, request.CustomPosition, out anchorBounds))
        {
            return false;
        }

        contentSize = MeasureContent(request.Content, anchorBounds);
        if (contentSize.Width <= 0 || contentSize.Height <= 0)
        {
            contentSize = new Size(Math.Max(1d, contentSize.Width), Math.Max(1d, contentSize.Height));
        }

        return true;
    }

    private Point ApplyStateAffinity(
        Rect anchorBounds,
        Size contentSize,
        double anchorVerticalMin,
        double anchorVerticalMax,
        ref PlacementRanges ranges,
        ViewportConstraints constraints,
        Point offset)
    {
        if (!this.state.HasValue)
        {
            return offset;
        }

        // Preserve the prior attachment bias while allowing the anchor to regain control when constraints relax.
        var horizontal = this.ApplyHorizontalAttachmentAffinity(anchorBounds, contentSize, ranges, constraints, offset.X);
        var vertical = this.ApplyVerticalAttachmentAffinity(anchorBounds, contentSize, anchorVerticalMin, anchorVerticalMax, ranges, constraints, offset.Y);

        horizontal = this.ApplyPointerSafety(contentSize, ref ranges, horizontal);

        return new Point(horizontal, vertical);
    }

    private double ApplyHorizontalAttachmentAffinity(
        Rect anchorBounds,
        Size contentSize,
        PlacementRanges ranges,
        ViewportConstraints constraints,
        double current)
    {
        return this.state.HorizontalAttachment switch
        {
            HorizontalAttachment.WindowLeading when constraints.HorizontalApplied => RestoreAnchorLeading(),
            HorizontalAttachment.WindowTrailing when constraints.HorizontalApplied => RestoreAnchorTrailing(),
            HorizontalAttachment.WindowLeading => ranges.ClampHorizontal(anchorBounds.Left),
            HorizontalAttachment.WindowTrailing => ranges.ClampHorizontal(anchorBounds.Right - contentSize.Width),
            HorizontalAttachment.AnchorLeading or HorizontalAttachment.CustomOrigin => ranges.ClampHorizontal(anchorBounds.Left),
            HorizontalAttachment.AnchorTrailing => ranges.ClampHorizontal(anchorBounds.Right - contentSize.Width),
            _ => ranges.ClampHorizontal(current),
        };

        double RestoreAnchorLeading()
        {
            // If the anchor fits again, prefer it over the previously forced window edge.
            var anchorOffset = ranges.ClampHorizontal(anchorBounds.Left);
            return AreClose(anchorOffset, constraints.HorizontalMin)
                ? ranges.ClampHorizontal(constraints.HorizontalMin)
                : anchorOffset;
        }

        double RestoreAnchorTrailing()
        {
            // Likewise, release the trailing edge once the anchor can accommodate the popup width.
            var anchorOffset = ranges.ClampHorizontal(anchorBounds.Right - contentSize.Width);
            return AreClose(anchorOffset, constraints.HorizontalMax)
                ? ranges.ClampHorizontal(constraints.HorizontalMax)
                : anchorOffset;
        }
    }

    private double ApplyVerticalAttachmentAffinity(
        Rect anchorBounds,
        Size contentSize,
        double anchorVerticalMin,
        double anchorVerticalMax,
        PlacementRanges ranges,
        ViewportConstraints constraints,
        double current)
    {
        return this.state.VerticalAttachment switch
        {
            VerticalAttachment.WindowTop when constraints.VerticalApplied => RestoreAnchorTop(),
            VerticalAttachment.WindowBottom when constraints.VerticalApplied => RestoreAnchorBottom(),
            VerticalAttachment.WindowTop => ranges.ClampVertical(anchorBounds.Top),
            VerticalAttachment.WindowBottom => ranges.ClampVertical(anchorBounds.Bottom - contentSize.Height),
            VerticalAttachment.AnchorTop => ranges.ClampVertical(anchorVerticalMin),
            VerticalAttachment.AnchorBottom => ranges.ClampVertical(anchorVerticalMax),
            VerticalAttachment.CustomOrigin => ranges.ClampVertical(anchorBounds.Top),
            _ => ranges.ClampVertical(current),
        };

        double RestoreAnchorTop()
        {
            // Prefer returning to the anchor once the window clamp is no longer required.
            var anchorOffset = ranges.ClampVertical(anchorBounds.Top);
            return AreClose(anchorOffset, constraints.VerticalMin)
                ? ranges.ClampVertical(constraints.VerticalMin)
                : anchorOffset;
        }

        double RestoreAnchorBottom()
        {
            // Same for the bottom edge: release the clamp if the anchor can fully host the popup height.
            var anchorOffset = ranges.ClampVertical(anchorBounds.Bottom - contentSize.Height);
            return AreClose(anchorOffset, constraints.VerticalMax)
                ? ranges.ClampVertical(constraints.VerticalMax)
                : anchorOffset;
        }
    }

    private double ApplyPointerSafety(Size contentSize, ref PlacementRanges ranges, double horizontal)
    {
        if (this.pointerPosition is not { } pointer)
        {
            return horizontal;
        }

        var previousBounds = new Rect(this.state.Offset, this.state.ContentSize);
        var pointerWithinPopup = previousBounds.Contains(pointer);
        if (!pointerWithinPopup && this.pointerItemBounds is Rect itemBounds)
        {
            pointerWithinPopup = itemBounds.Contains(pointer);
        }

        if (!pointerWithinPopup)
        {
            return horizontal;
        }

        // Keep the pointer outside the popup by intersecting the current range with the pointer exclusion band.
        var pointerRangeMin = pointer.X + PointerSafetyMargin - contentSize.Width;
        var pointerRangeMax = pointer.X - PointerSafetyMargin;

        if (pointerRangeMin <= pointerRangeMax)
        {
            ranges.IntersectHorizontal(pointerRangeMin, pointerRangeMax);
        }
        else
        {
            var fallbackMin = pointer.X - contentSize.Width + PointerEdgePadding;
            var fallbackMax = pointer.X - PointerEdgePadding;
            ranges.IntersectHorizontal(fallbackMin, fallbackMax);
        }

        return ranges.ClampHorizontal(horizontal);
    }

    private void EnsureStateForToken(int token)
    {
        if (this.state.Token != token)
        {
            this.state = PlacementState.Empty;
        }
    }

    /// <summary>
    ///     Describes the data required to calculate popup placement for a specific anchor and content pair.
    /// </summary>
    [StructLayout(LayoutKind.Auto)]
    internal readonly struct PlacementRequest
    {
        /// <summary>
        ///     Initializes a new instance of the <see cref="PlacementRequest"/> struct.
        /// </summary>
        /// <param name="token">Monotonic token identifying the logical placement session.</param>
        /// <param name="anchor">Visual that provides the placement anchor.</param>
        /// <param name="content">Popup content element to position.</param>
        /// <param name="viewport">Visible viewport available for placement.</param>
        /// <param name="customPosition">Optional custom anchor offset relative to the anchor.</param>
        public PlacementRequest(int token, FrameworkElement anchor, UIElement content, Rect viewport, Point? customPosition)
        {
            this.Token = token;
            this.Anchor = anchor;
            this.Content = content;
            this.Viewport = viewport;
            this.CustomPosition = customPosition;
        }

        /// <summary>
        ///     Gets the placement session token.
        /// </summary>
        public int Token { get; }

        /// <summary>
        ///     Gets the anchor element driving placement.
        /// </summary>
        public FrameworkElement Anchor { get; }

        /// <summary>
        ///     Gets the popup content being positioned.
        /// </summary>
        public UIElement Content { get; }

        /// <summary>
        ///     Gets the viewport constraints available for placement.
        /// </summary>
        public Rect Viewport { get; }

        /// <summary>
        ///     Gets the optional custom position relative to the anchor.
        /// </summary>
        public Point? CustomPosition { get; }
    }

    /// <summary>
    ///     Represents the resolved placement information for a popup.
    /// </summary>
    [StructLayout(LayoutKind.Auto)]
    internal readonly struct PlacementResult
    {
        /// <summary>
        ///     Initializes a new instance of the <see cref="PlacementResult"/> struct.
        /// </summary>
        /// <param name="offset">Final window offset for the popup.</param>
        /// <param name="size">Resolved content size used for placement.</param>
        /// <param name="anchorBounds">Computed anchor bounds in window coordinates.</param>
        public PlacementResult(Point offset, Size size, Rect anchorBounds)
        {
            this.Offset = offset;
            this.ContentSize = size;
            this.AnchorBounds = anchorBounds;
        }

        /// <summary>
        ///     Gets the placement offset in window coordinates.
        /// </summary>
        public Point Offset { get; }

        /// <summary>
        ///     Gets the size used when positioning the content.
        /// </summary>
        public Size ContentSize { get; }

        /// <summary>
        ///     Gets the anchor bounds used to derive placement.
        /// </summary>
        public Rect AnchorBounds { get; }
    }

    /// <summary>
    ///     Represents the allowable horizontal and vertical ranges for popup placement.
    /// </summary>
    [StructLayout(LayoutKind.Auto)]
    private struct PlacementRanges
    {
        /// <summary>
        ///     Initializes a new instance of the <see cref="PlacementRanges"/> struct.
        /// </summary>
        /// <param name="horizontalMin">Minimum horizontal offset.</param>
        /// <param name="horizontalMax">Maximum horizontal offset.</param>
        /// <param name="verticalMin">Minimum vertical offset.</param>
        /// <param name="verticalMax">Maximum vertical offset.</param>
        public PlacementRanges(double horizontalMin, double horizontalMax, double verticalMin, double verticalMax)
        {
            if (horizontalMin <= horizontalMax)
            {
                this.HorizontalMin = horizontalMin;
                this.HorizontalMax = horizontalMax;
            }
            else
            {
                this.HorizontalMin = horizontalMax;
                this.HorizontalMax = horizontalMin;
            }

            if (verticalMin <= verticalMax)
            {
                this.VerticalMin = verticalMin;
                this.VerticalMax = verticalMax;
            }
            else
            {
                this.VerticalMin = verticalMax;
                this.VerticalMax = verticalMin;
            }
        }

        /// <summary>
        ///     Gets the minimum horizontal offset permitted.
        /// </summary>
        public double HorizontalMin { get; private set; }

        /// <summary>
        ///     Gets the maximum horizontal offset permitted.
        /// </summary>
        public double HorizontalMax { get; private set; }

        /// <summary>
        ///     Gets the minimum vertical offset permitted.
        /// </summary>
        public double VerticalMin { get; private set; }

        /// <summary>
        ///     Gets the maximum vertical offset permitted.
        /// </summary>
        public double VerticalMax { get; private set; }

        /// <summary>
        ///     Intersects the horizontal range with the specified window constraints.
        /// </summary>
        /// <param name="min">Proposed minimum horizontal offset.</param>
        /// <param name="max">Proposed maximum horizontal offset.</param>
        public void IntersectHorizontal(double min, double max)
        {
            var orderedMin = Math.Min(min, max);
            var orderedMax = Math.Max(min, max);

            var intersectionMin = Math.Max(this.HorizontalMin, orderedMin);
            var intersectionMax = Math.Min(this.HorizontalMax, orderedMax);
            if (intersectionMin <= intersectionMax)
            {
                this.HorizontalMin = intersectionMin;
                this.HorizontalMax = intersectionMax;
            }
            else
            {
                this.HorizontalMin = orderedMin;
                this.HorizontalMax = orderedMax;
            }
        }

        /// <summary>
        ///     Intersects the vertical range with the specified window constraints.
        /// </summary>
        /// <param name="min">Proposed minimum vertical offset.</param>
        /// <param name="max">Proposed maximum vertical offset.</param>
        public void IntersectVertical(double min, double max)
        {
            var orderedMin = Math.Min(min, max);
            var orderedMax = Math.Max(min, max);

            var intersectionMin = Math.Max(this.VerticalMin, orderedMin);
            var intersectionMax = Math.Min(this.VerticalMax, orderedMax);
            if (intersectionMin <= intersectionMax)
            {
                this.VerticalMin = intersectionMin;
                this.VerticalMax = intersectionMax;
            }
            else
            {
                this.VerticalMin = orderedMin;
                this.VerticalMax = orderedMax;
            }
        }

        /// <summary>
        ///     Clamps the horizontal offset within the allowed range.
        /// </summary>
        /// <param name="value">Candidate horizontal offset.</param>
        /// <returns>Offset constrained to the horizontal range.</returns>
        public double ClampHorizontal(double value) => SafeClamp(value, this.HorizontalMin, this.HorizontalMax);

        /// <summary>
        ///     Clamps the vertical offset within the allowed range.
        /// </summary>
        /// <param name="value">Candidate vertical offset.</param>
        /// <returns>Offset constrained to the vertical range.</returns>
        public double ClampVertical(double value) => SafeClamp(value, this.VerticalMin, this.VerticalMax);
    }

    /// <summary>
    ///     Captures viewport-derived constraints applied during placement.
    /// </summary>
    [StructLayout(LayoutKind.Auto)]
    private readonly struct ViewportConstraints
    {
        /// <summary>
        ///     Initializes a new instance of the <see cref="ViewportConstraints"/> struct.
        /// </summary>
        /// <param name="horizontalApplied">Indicates whether horizontal constraints were applied.</param>
        /// <param name="horizontalMin">Window horizontal minimum offset.</param>
        /// <param name="horizontalMax">Window horizontal maximum offset.</param>
        /// <param name="verticalApplied">Indicates whether vertical constraints were applied.</param>
        /// <param name="verticalMin">Window vertical minimum offset.</param>
        /// <param name="verticalMax">Window vertical maximum offset.</param>
        public ViewportConstraints(bool horizontalApplied, double horizontalMin, double horizontalMax, bool verticalApplied, double verticalMin, double verticalMax)
        {
            this.HorizontalApplied = horizontalApplied;
            this.HorizontalMin = horizontalMin;
            this.HorizontalMax = horizontalMax;
            this.VerticalApplied = verticalApplied;
            this.VerticalMin = verticalMin;
            this.VerticalMax = verticalMax;
        }

        /// <summary>
        ///     Gets a value indicating whether horizontal constraints were applied.
        /// </summary>
        public bool HorizontalApplied { get; }

        /// <summary>
        ///     Gets the window-aligned horizontal minimum offset.
        /// </summary>
        public double HorizontalMin { get; }

        /// <summary>
        ///     Gets the window-aligned horizontal maximum offset.
        /// </summary>
        public double HorizontalMax { get; }

        /// <summary>
        ///     Gets a value indicating whether vertical constraints were applied.
        /// </summary>
        public bool VerticalApplied { get; }

        /// <summary>
        ///     Gets the window-aligned vertical minimum offset.
        /// </summary>
        public double VerticalMin { get; }

        /// <summary>
        ///     Gets the window-aligned vertical maximum offset.
        /// </summary>
        public double VerticalMax { get; }
    }

    /// <summary>
    ///     Encapsulates the attachment modes resolved for a placement.
    /// </summary>
    [StructLayout(LayoutKind.Auto)]
    private readonly struct AttachmentResult
    {
        /// <summary>
        ///     Initializes a new instance of the <see cref="AttachmentResult"/> struct.
        /// </summary>
        /// <param name="horizontal">Horizontal attachment resolved for the placement.</param>
        /// <param name="vertical">Vertical attachment resolved for the placement.</param>
        public AttachmentResult(HorizontalAttachment horizontal, VerticalAttachment vertical)
        {
            this.Horizontal = horizontal;
            this.Vertical = vertical;
        }

        /// <summary>
        ///     Gets the horizontal placement attachment.
        /// </summary>
        public HorizontalAttachment Horizontal { get; }

        /// <summary>
        ///     Gets the vertical placement attachment.
        /// </summary>
        public VerticalAttachment Vertical { get; }
    }

    /// <summary>
    ///     Tracks placement affinity across subsequent layout passes.
    /// </summary>
    [StructLayout(LayoutKind.Auto)]
    private readonly struct PlacementState
    {
        /// <summary>
        ///     Initializes a new instance of the <see cref="PlacementState"/> struct.
        /// </summary>
        /// <param name="token">Token identifying the placement session.</param>
        /// <param name="offset">Last resolved offset.</param>
        /// <param name="size">Last resolved content size.</param>
        /// <param name="horizontalAttachment">Horizontal affinity maintained across updates.</param>
        /// <param name="verticalAttachment">Vertical affinity maintained across updates.</param>
        public PlacementState(int token, Point offset, Size size, HorizontalAttachment horizontalAttachment, VerticalAttachment verticalAttachment)
        {
            this.Token = token;
            this.Offset = offset;
            this.ContentSize = size;
            this.HorizontalAttachment = horizontalAttachment;
            this.VerticalAttachment = verticalAttachment;
        }

        /// <summary>
        ///     Gets an empty placement state instance.
        /// </summary>
        public static PlacementState Empty => default;

        /// <summary>
        ///     Gets the session token associated with the state.
        /// </summary>
        public int Token { get; }

        /// <summary>
        ///     Gets the previously resolved offset.
        /// </summary>
        public Point Offset { get; }

        /// <summary>
        ///     Gets the previously resolved content size.
        /// </summary>
        public Size ContentSize { get; }

        /// <summary>
        ///     Gets the horizontal attachment affinity.
        /// </summary>
        public HorizontalAttachment HorizontalAttachment { get; }

        /// <summary>
        ///     Gets the vertical attachment affinity.
        /// </summary>
        public VerticalAttachment VerticalAttachment { get; }

        /// <summary>
        ///     Gets a value indicating whether the state carries valid placement data.
        /// </summary>
        public bool HasValue => this.Token != 0;
    }
}
