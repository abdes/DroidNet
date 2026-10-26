// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>
/// Rendering and row-lookup helpers shared by the tree tests that inspect realized rows.
/// </summary>
internal static class TreeRenderHelpers
{
    /// <summary>Waits for the next composition frame.</summary>
    /// <returns>A task that completes after the frame rendered.</returns>
    public static async Task RenderAsync()
        => _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

    /// <summary>Waits for <paramref name="frames"/> composition frames.</summary>
    /// <param name="frames">The number of frames to wait for.</param>
    /// <returns>A task that completes after the last frame rendered.</returns>
    public static async Task RenderAsync(int frames)
    {
        for (var frame = 0; frame < frames; frame++)
        {
            await RenderAsync().ConfigureAwait(true);
        }
    }

    /// <summary>Gets the rows the tree has realized, in visual-tree order.</summary>
    /// <remarks>
    /// A row the repeater cleared is detached from the tree and no longer realized, even if the repeater still keeps
    /// it in the visual tree for reuse.
    /// </remarks>
    /// <param name="tree">The tree to inspect.</param>
    /// <returns>The realized rows.</returns>
    public static List<TreeItemControl> Rows(DynamicTree tree)
        => [.. tree.FindDescendants().OfType<TreeItemControl>().Where(static row => row.OwnerTree is not null)];

    /// <summary>Gets the rows the repeater has realized among its first <paramref name="count"/> items.</summary>
    /// <param name="repeater">The tree's items repeater.</param>
    /// <param name="count">The number of items the repeater shows.</param>
    /// <returns>The index, container and row of each realized item.</returns>
    public static List<(int Index, FrameworkElement Container, TreeItemControl Row)> RealizedRows(ItemsRepeater repeater, int count)
    {
        var realized = new List<(int, FrameworkElement, TreeItemControl)>();
        for (var index = 0; index < count; index++)
        {
            if (repeater.TryGetElement(index) is FrameworkElement container
                && container.FindName(DynamicTree.TreeItemPart) is TreeItemControl row)
            {
                realized.Add((index, container, row));
            }
        }

        return realized;
    }

    /// <summary>Scrolls without animation and renders until the scroller reaches <paramref name="offset"/>.</summary>
    /// <param name="scroller">The tree's scroll view.</param>
    /// <param name="offset">The vertical offset to reach.</param>
    /// <returns>A task that completes when the offset is reached and the rows around it rendered.</returns>
    public static async Task ScrollToAndRenderAsync(ScrollView scroller, double offset)
    {
        _ = scroller.ScrollTo(0, offset, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
        for (var frame = 0; frame < 20; frame++)
        {
            await RenderAsync().ConfigureAwait(true);
            if (Math.Abs(scroller.VerticalOffset - offset) <= 1)
            {
                await RenderAsync().ConfigureAwait(true);
                return;
            }
        }

        _ = scroller.VerticalOffset.Should().BeApproximately(offset, 1);
    }
}
