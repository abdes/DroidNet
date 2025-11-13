// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using AwesomeAssertions;
using Microsoft.UI;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace DroidNet.Controls.Menus.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("PopupPlacementHelperTests")]
[TestCategory("UITest")]
public sealed class PopupPlacementHelperTests : VisualUserInterfaceTests
{
    private const double CanvasWidth = 400d;
    private const double CanvasHeight = 240d;
    private const double DefaultAnchorWidth = 40d;
    private const double DefaultAnchorHeight = 24d;
    private const double WindowEdgePadding = 4d; // Mirrors helper padding so expectations stay readable.

    [TestMethod]
    public Task PrefersAnchorAlignmentWithAmpleSpace_Async() => EnqueueAsync(async () =>
    {
        var (_, anchors) = await CreateAnchorsAsync((120d, 40d)).ConfigureAwait(true);
        var anchor = anchors[0];

        var helper = new PopupPlacementHelper();
        var content = CreateContent(120d, 80d);
        var viewport = new Rect(0, 0, CanvasWidth, CanvasHeight);

        var request = new PopupPlacementHelper.PlacementRequest(1, anchor, content, viewport, customPosition: null);
        var success = helper.TryPlace(request, out var result);

        _ = success.Should().BeTrue();
        _ = result.Offset.X.Should().BeApproximately(result.AnchorBounds.Left, 0.5);
        _ = result.Offset.Y.Should().BeApproximately(result.AnchorBounds.Bottom, 0.5);
        _ = result.ContentSize.Should().BeEquivalentTo(new Size(120d, 80d));
    });

    [TestMethod]
    public Task ClampsToWindowEdgeWhenSpaceIsLimited_Async() => EnqueueAsync(async () =>
    {
        var (_, anchors) = await CreateAnchorsAsync((260d, 40d)).ConfigureAwait(true);
        var anchor = anchors[0];

        var helper = new PopupPlacementHelper();
        var content = CreateContent(120d, 80d);
        var constrainedViewport = new Rect(0, 0, 300d, CanvasHeight);

        var request = new PopupPlacementHelper.PlacementRequest(1, anchor, content, constrainedViewport, customPosition: null);
        _ = helper.TryPlace(request, out var result).Should().BeTrue();

        var expectedRightEdge = constrainedViewport.Width - WindowEdgePadding;
        _ = (result.Offset.X + result.ContentSize.Width).Should().BeApproximately(expectedRightEdge, 0.5);
        _ = result.Offset.X.Should().BeLessThan(
            result.AnchorBounds.Left,
            "clamping near the window edge should shift the popup left of the anchor");
    });

    [TestMethod]
    public Task RealignsWithAnchorWhenConstraintsRelax_Async() => EnqueueAsync(async () =>
    {
        var (_, anchors) = await CreateAnchorsAsync((260d, 40d)).ConfigureAwait(true);
        var anchor = anchors[0];

        var helper = new PopupPlacementHelper();
        var content = CreateContent(120d, 80d);
        var constrainedViewport = new Rect(0, 0, 300d, CanvasHeight);
        var relaxedViewport = new Rect(0, 0, CanvasWidth, CanvasHeight);

        var constrainedRequest = new PopupPlacementHelper.PlacementRequest(1, anchor, content, constrainedViewport, customPosition: null);
        _ = helper.TryPlace(constrainedRequest, out var constrainedResult).Should().BeTrue();

        var relaxedRequest = new PopupPlacementHelper.PlacementRequest(1, anchor, content, relaxedViewport, customPosition: null);
        _ = helper.TryPlace(relaxedRequest, out var relaxedResult).Should().BeTrue();

        var expectedTrailing = relaxedResult.AnchorBounds.Right - relaxedResult.ContentSize.Width;
        _ = relaxedResult.Offset.X.Should().BeApproximately(expectedTrailing, 0.5);
        _ = relaxedResult.Offset.X.Should().BeGreaterThan(constrainedResult.Offset.X);
    });

    [TestMethod]
    public Task PointerTrackingKeepsHoveredItemStable_Async() => EnqueueAsync(async () =>
    {
        var (_, anchors) = await CreateAnchorsAsync((220d, 40d), (20d, 40d)).ConfigureAwait(true);
        var rightAnchor = anchors[0];
        var leftAnchor = anchors[1];

        var helper = new PopupPlacementHelper();
        var content = CreateContent(120d, 80d);
        var viewport = new Rect(0, 0, CanvasWidth, CanvasHeight);

        var initialRequest = new PopupPlacementHelper.PlacementRequest(1, rightAnchor, content, viewport, customPosition: null);
        _ = helper.TryPlace(initialRequest, out var initialResult).Should().BeTrue();

        var pointer = new Point(initialResult.Offset.X + (initialResult.ContentSize.Width / 2), initialResult.Offset.Y + 10d);
        helper.UpdatePointer(pointer);

        var followRequest = new PopupPlacementHelper.PlacementRequest(1, leftAnchor, content, viewport, customPosition: null);
        _ = helper.TryPlace(followRequest, out var followResult).Should().BeTrue();

        var popupBounds = new Rect(followResult.Offset, followResult.ContentSize);
        _ = popupBounds.Contains(pointer).Should().BeTrue("pointer should remain within the popup bounds after repositioning");
        _ = followResult.Offset.X.Should().BeGreaterThan(followResult.AnchorBounds.Left, "pointer safety should override anchor-leading alignment when necessary");
    });

    [TestMethod]
    public Task CustomOriginPlacementAlignsToProvidedPoint_Async() => EnqueueAsync(async () =>
    {
        var customPoint = new Point(30d, 12d);
        var (_, anchors) = await CreateAnchorsAsync((150d, 80d)).ConfigureAwait(true);
        var anchor = anchors[0];

        var helper = new PopupPlacementHelper();
        var content = CreateContent(60d, 40d);
        var viewport = new Rect(0, 0, CanvasWidth, CanvasHeight);

        var request = new PopupPlacementHelper.PlacementRequest(1, anchor, content, viewport, customPoint);
        _ = helper.TryPlace(request, out var result).Should().BeTrue();

        _ = result.Offset.X.Should().BeApproximately(result.AnchorBounds.Left, 0.5);
        _ = result.Offset.Y.Should().BeApproximately(result.AnchorBounds.Top, 0.5);
        _ = result.AnchorBounds.Width.Should().Be(1d);
        _ = result.AnchorBounds.Height.Should().Be(1d);
    });

    [TestMethod]
    public Task ClampsVerticallyNearWindowEdges_Async() => EnqueueAsync(async () =>
    {
        const double bottomAnchorTop = CanvasHeight - DefaultAnchorHeight - 8d;
        var (_, anchors) = await CreateAnchorsAsync((120d, bottomAnchorTop)).ConfigureAwait(true);
        var anchor = anchors[0];

        var helper = new PopupPlacementHelper();
        var content = CreateContent(100d, 140d);
        var viewport = new Rect(0, 0, CanvasWidth, CanvasHeight);

        var request = new PopupPlacementHelper.PlacementRequest(1, anchor, content, viewport, customPosition: null);
        _ = helper.TryPlace(request, out var result).Should().BeTrue();

        var expectedBottom = viewport.Height - WindowEdgePadding;
        _ = (result.Offset.Y + result.ContentSize.Height).Should().BeApproximately(expectedBottom, 0.5);
        _ = result.Offset.Y.Should().BeLessThan(
            result.AnchorBounds.Bottom,
            "clamping near the window bottom should shift the popup upward relative to the anchor");
    });

    [TestMethod]
    public Task NewTokenResetsPlacementState_Async() => EnqueueAsync(async () =>
    {
        var (_, anchors) = await CreateAnchorsAsync((220d, 40d), (20d, 40d)).ConfigureAwait(true);
        var rightAnchor = anchors[0];
        var leftAnchor = anchors[1];

        var helper = new PopupPlacementHelper();
        var content = CreateContent(120d, 80d);
        var viewport = new Rect(0, 0, CanvasWidth, CanvasHeight);

        var initialRequest = new PopupPlacementHelper.PlacementRequest(1, rightAnchor, content, viewport, customPosition: null);
        _ = helper.TryPlace(initialRequest, out var initialResult).Should().BeTrue();

        var pointer = new Point(initialResult.Offset.X + (initialResult.ContentSize.Width / 2), initialResult.Offset.Y + 5d);
        helper.UpdatePointer(pointer);

        var pointerSafeRequest = new PopupPlacementHelper.PlacementRequest(1, leftAnchor, content, viewport, customPosition: null);
        _ = helper.TryPlace(pointerSafeRequest, out var pointerSafeResult).Should().BeTrue();
        _ = pointerSafeResult.Offset.X.Should().BeGreaterThan(pointerSafeResult.AnchorBounds.Left);

        var resetRequest = new PopupPlacementHelper.PlacementRequest(2, leftAnchor, content, viewport, customPosition: null);
        _ = helper.TryPlace(resetRequest, out var resetResult).Should().BeTrue();

        _ = resetResult.Offset.X.Should().BeApproximately(
            resetResult.AnchorBounds.Left,
            0.5,
            "changing the token should clear pointer-driven state and allow anchor-leading alignment");
        _ = resetResult.Offset.X.Should().BeLessThan(pointerSafeResult.Offset.X);
    });

    private static Border CreateContent(double width, double height) => new()
    {
        Width = width,
        Height = height,
        Background = new SolidColorBrush(Colors.SlateGray),
    };

    private static async Task<(Canvas root, IReadOnlyList<Border> anchors)> CreateAnchorsAsync(params (double left, double top)[] positions)
    {
        var canvas = new Canvas
        {
            Width = CanvasWidth,
            Height = CanvasHeight,
            Background = new SolidColorBrush(Colors.Transparent),
        };

        var anchors = new List<Border>(positions.Length);
        foreach (var (left, top) in positions)
        {
            var anchor = new Border
            {
                Width = DefaultAnchorWidth,
                Height = DefaultAnchorHeight,
                Background = new SolidColorBrush(Colors.LightGray),
            };

            Canvas.SetLeft(anchor, left);
            Canvas.SetTop(anchor, top);
            canvas.Children.Add(anchor);
            anchors.Add(anchor);
        }

        await LoadTestContentAsync(canvas).ConfigureAwait(true);
        return (canvas, anchors);
    }
}
