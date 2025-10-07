// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using MenuItemControl = DroidNet.Controls.Menus.MenuItem;

namespace DroidNet.Controls.Menus.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("PopupMenuHostTests")]
[TestCategory("UITest")]
public sealed class PopupMenuHostTests : VisualUserInterfaceTests
{
    private static readonly TimeSpan EventTimeout = TimeSpan.FromSeconds(2);
    private static readonly TimeSpan DebounceAllowance = TimeSpan.FromMilliseconds(30);

    [TestMethod]
    public Task OpensPopupAfterDebounce_Async() => EnqueueAsync(async () =>
    {
        var context = await PopupMenuHostTestContext.CreateAsync(2).ConfigureAwait(true);

        try
        {
            var openingCount = 0;
            var openedTcs = CreateSignal();
            context.Host.Opening += (_, _) => openingCount++;
            context.Host.Opened += (_, _) => openedTcs.TrySetResult(true);

            context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.PointerInput);

            await WaitForEventAsync(openedTcs.Task, "Popup should open after debounce").ConfigureAwait(true);
            _ = openingCount.Should().Be(1);
            _ = context.Host.IsOpen.Should().BeTrue();
        }
        finally
        {
            context.Dispose();
        }
    });

    [TestMethod]
    public Task DismissBeforeOpenRaisesClosingAndClosed_Async() => EnqueueAsync(async () =>
    {
        var context = await PopupMenuHostTestContext.CreateAsync(1).ConfigureAwait(true);

        try
        {
            var closingTcs = CreateSignal();
            var closedTcs = CreateSignal();
            var opened = false;
            context.Host.Closing += (_, _) => closingTcs.TrySetResult(true);
            context.Host.Closed += (_, _) => closedTcs.TrySetResult(true);
            context.Host.Opened += (_, _) => opened = true;

            context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.PointerInput);
            context.Host.Dismiss(MenuDismissKind.PointerInput);

            await WaitForEventAsync(closingTcs.Task, "Closing should fire when canceling pending open").ConfigureAwait(true);
            await WaitForEventAsync(closedTcs.Task, "Closed should fire when pending open is canceled").ConfigureAwait(true);
            _ = opened.Should().BeFalse("Popup should never open when dismissed before debounce elapses");
            _ = context.Host.IsOpen.Should().BeFalse();
        }
        finally
        {
            context.Dispose();
        }
    });

    [TestMethod]
    public Task DismissWhileOpenClosesPopup_Async() => EnqueueAsync(async () =>
    {
        var context = await PopupMenuHostTestContext.CreateAsync(1).ConfigureAwait(true);

        try
        {
            var openedTcs = CreateSignal();
            var closedTcs = CreateSignal();
            context.Host.Opened += (_, _) => openedTcs.TrySetResult(true);
            context.Host.Closed += (_, _) => closedTcs.TrySetResult(true);

            context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.PointerInput);

            await WaitForEventAsync(openedTcs.Task, "Popup should open before dismissal").ConfigureAwait(true);
            _ = context.Host.IsOpen.Should().BeTrue();

            context.Host.Dismiss(MenuDismissKind.PointerInput);

            await WaitForEventAsync(closedTcs.Task, "Popup should close after dismissal").ConfigureAwait(true);
            _ = context.Host.IsOpen.Should().BeFalse();
        }
        finally
        {
            context.Dispose();
        }
    });

    [TestMethod]
    public Task ShowAtWhileOpenReanchorsWithoutClosing_Async() => EnqueueAsync(async () =>
    {
        var context = await PopupMenuHostTestContext.CreateAsync(2).ConfigureAwait(true);

        try
        {
            var openedTcs = CreateSignal();
            context.Host.Opened += (_, _) => openedTcs.TrySetResult(true);

            context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.PointerInput);
            await WaitForEventAsync(openedTcs.Task, "Popup should open for first anchor").ConfigureAwait(true);

            var closed = false;
            var openingCount = 0;
            context.Host.Closed += (_, _) => closed = true;
            context.Host.Opening += (_, _) => openingCount++;

            context.Host.ShowAt(context.Anchors[1], MenuNavigationMode.PointerInput);
            await Task.Delay(DebounceAllowance).ConfigureAwait(true);

            _ = closed.Should().BeFalse("Popup should remain open when reanchoring");
            _ = openingCount.Should().Be(1, "Opening should be raised for the reanchor request");
            _ = context.Host.IsOpen.Should().BeTrue();
            _ = context.Host.Anchor.Should().Be(context.Anchors[1]);
        }
        finally
        {
            context.Dispose();
        }
    });

    [TestMethod]
    public Task LatestShowAtWinsBeforeOpen_Async() => EnqueueAsync(async () =>
    {
        var context = await PopupMenuHostTestContext.CreateAsync(2).ConfigureAwait(true);

        try
        {
            var openingAnchors = new List<MenuItemControl?>();
            var openedTcs = CreateSignal();

            context.Host.Opening += (_, _) => openingAnchors.Add(context.Host.Anchor as MenuItemControl);
            context.Host.Opened += (_, _) => openedTcs.TrySetResult(true);

            context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.PointerInput);
            context.Host.ShowAt(context.Anchors[1], MenuNavigationMode.PointerInput);

            await WaitForEventAsync(openedTcs.Task, "Popup should open using the latest pending anchor").ConfigureAwait(true);

            _ = openingAnchors.Should().NotBeEmpty();
            _ = openingAnchors.Last().Should().Be(context.Anchors[1]);
            _ = context.Host.Anchor.Should().Be(context.Anchors[1]);
        }
        finally
        {
            context.Dispose();
        }
    });

    [TestMethod]
    public Task DismissCancellationKeepsPopupOpen_Async() => EnqueueAsync(async () =>
    {
        var context = await PopupMenuHostTestContext.CreateAsync(1).ConfigureAwait(true);

        try
        {
            var openedTcs = CreateSignal();
            var closingCount = 0;
            var closed = false;

            context.Host.Opened += (_, _) => openedTcs.TrySetResult(true);
            context.Host.Closing += (_, args) =>
            {
                closingCount++;
                args.Cancel = true;
            };
            context.Host.Closed += (_, _) => closed = true;

            context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.PointerInput);
            await WaitForEventAsync(openedTcs.Task, "Popup should open before cancellation test").ConfigureAwait(true);

            context.Host.Dismiss(MenuDismissKind.PointerInput);
            await Task.Delay(DebounceAllowance).ConfigureAwait(true);

            _ = closingCount.Should().Be(1, "Canceling dismissal should still raise Closing once");
            _ = context.Host.IsOpen.Should().BeTrue("Canceled dismissal should leave popup open");
            _ = closed.Should().BeFalse("Canceled dismissal should not raise Closed");
        }
        finally
        {
            context.Dispose();
        }
    });

    [TestMethod]
    public Task RedundantDismissWhileClosingIsIgnored_Async() => EnqueueAsync(async () =>
    {
        var context = await PopupMenuHostTestContext.CreateAsync(1).ConfigureAwait(true);

        try
        {
            var openedTcs = CreateSignal();
            var closedTcs = CreateSignal();
            var closingCount = 0;
            var closedCount = 0;

            context.Host.Opened += (_, _) => openedTcs.TrySetResult(true);
            context.Host.Closing += (_, _) => closingCount++;
            context.Host.Closed += (_, _) =>
            {
                closedCount++;
                closedTcs.TrySetResult(true);
            };

            context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.PointerInput);
            await WaitForEventAsync(openedTcs.Task, "Popup should open before redundant dismiss test").ConfigureAwait(true);

            context.Host.Dismiss(MenuDismissKind.PointerInput);
            context.Host.Dismiss(MenuDismissKind.KeyboardInput);

            await WaitForEventAsync(closedTcs.Task, "Popup should still close once after redundant dismiss").ConfigureAwait(true);

            _ = closingCount.Should().Be(1, "Second dismiss while closing should be ignored");
            _ = closedCount.Should().Be(1, "Popup should raise Closed only once");
        }
        finally
        {
            context.Dispose();
        }
    });

    [TestMethod]
    public Task RapidHoverCyclesKeepPopupResponsive_Async() => EnqueueAsync(async () =>
    {
        const int hoverCount = 20;
        var context = await PopupMenuHostTestContext.CreateAsync(4).ConfigureAwait(true);

        try
        {
            var openedTcs = CreateSignal();
            var closingCount = 0;
            var closedCount = 0;
            var openingAnchors = new List<MenuItemControl?>();

            context.Host.Opened += (_, _) => openedTcs.TrySetResult(true);
            context.Host.Opening += (_, _) => openingAnchors.Add(context.Host.Anchor as MenuItemControl);
            context.Host.Closing += (_, _) => closingCount++;
            context.Host.Closed += (_, _) => closedCount++;

            context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.PointerInput);
            await WaitForEventAsync(openedTcs.Task, "Popup should open before hover cycling").ConfigureAwait(true);

            var lastAnchorIndex = 0;
            for (var i = 0; i < hoverCount; i++)
            {
                lastAnchorIndex = (i % (context.Anchors.Count - 1)) + 1;
                var anchor = context.Anchors[lastAnchorIndex];
                context.Host.ShowAt(anchor, MenuNavigationMode.PointerInput);
                await Task.Delay(DebounceAllowance).ConfigureAwait(true);
            }

            _ = context.Host.IsOpen.Should().BeTrue("Popup should stay open during rapid hover cycling");
            _ = context.Host.Anchor.Should().Be(context.Anchors[lastAnchorIndex], "Popup anchor should be the latest hovered item");
            _ = closingCount.Should().Be(0, "No closing should occur during hover cycling");
            _ = closedCount.Should().Be(0, "Popup should not close during hover cycling");
            _ = openingAnchors.Should().HaveCount(hoverCount + 1, "Opening event should fire for each hover including the initial open");
        }
        finally
        {
            context.Dispose();
        }
    });

    [TestMethod]
    public Task DismissWithPendingReopenDefersClose_Async() => EnqueueAsync(async () =>
    {
        var context = await PopupMenuHostTestContext.CreateAsync(2).ConfigureAwait(true);

        try
        {
            var firstOpenedTcs = CreateSignal();
            context.Host.Opened += (_, _) => firstOpenedTcs.TrySetResult(true);
            context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.PointerInput);
            await WaitForEventAsync(firstOpenedTcs.Task, "Initial open should succeed").ConfigureAwait(true);

            var closedCount = 0;
            context.Host.Closed += (_, _) => closedCount++;

            context.Host.ShowAt(context.Anchors[1], MenuNavigationMode.PointerInput);
            context.Host.Dismiss(MenuDismissKind.PointerInput);

            await Task.Delay(DebounceAllowance + DebounceAllowance).ConfigureAwait(true);

            _ = closedCount.Should().Be(0, "Popup should stay open while a newer request is pending");
            _ = context.Host.IsOpen.Should().BeTrue();
            _ = context.Host.Anchor.Should().Be(context.Anchors[1]);

            var finalClosedTcs = CreateSignal();
            context.Host.Closed += (_, _) => finalClosedTcs.TrySetResult(true);
            context.Host.Dismiss(MenuDismissKind.Programmatic);

            await WaitForEventAsync(finalClosedTcs.Task, "Popup should eventually close after dismissal without pending request").ConfigureAwait(true);
            _ = context.Host.IsOpen.Should().BeFalse();
        }
        finally
        {
            context.Dispose();
        }
    });

    [TestMethod]
    public Task KeyboardDismissRestoresAnchorFocus_Async() => EnqueueAsync(async () =>
    {
        var context = await PopupMenuHostTestContext.CreateAsync(1).ConfigureAwait(true);

        try
        {
            var openedTcs = CreateSignal();
            context.Host.Opened += (_, _) => openedTcs.TrySetResult(true);

            context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.KeyboardInput);
            await WaitForEventAsync(openedTcs.Task, "Popup should open before keyboard dismissal").ConfigureAwait(true);

            var closedTcs = CreateSignal();
            context.Host.Closed += (_, _) => closedTcs.TrySetResult(true);

            context.Host.Dismiss(MenuDismissKind.KeyboardInput);

            await WaitForEventAsync(closedTcs.Task, "Popup should close after keyboard dismissal").ConfigureAwait(true);

            _ = context.Host.IsOpen.Should().BeFalse();

            await WaitForRenderCompletionAsync().ConfigureAwait(true);

            var focusedElement = FocusManager.GetFocusedElement(context.Anchors[0].XamlRoot);
            _ = focusedElement.Should().Be(context.Anchors[0], "Keyboard dismissal should restore focus to the anchor");
        }
        finally
        {
            context.Dispose();
        }
    });

    [TestMethod]
    public Task ProgrammaticFocusCyclingDoesNotClosePopup_Async() => EnqueueAsync(async () =>
    {
        const int anchorCount = 5;
        var context = await PopupMenuHostTestContext.CreateAsync(anchorCount).ConfigureAwait(true);

        try
        {
            var openedTcs = CreateSignal();
            context.Host.Opened += (_, _) => openedTcs.TrySetResult(true);

            context.Host.ShowAt(context.Anchors[0], MenuNavigationMode.Programmatic);
            await WaitForEventAsync(openedTcs.Task, "Popup should open before focus cycling").ConfigureAwait(true);

            var closedCount = 0;
            context.Host.Closed += (_, _) => closedCount++;

            var cycleDelay = TimeSpan.FromMilliseconds(Math.Max(1, DebounceAllowance.TotalMilliseconds / 3));

            for (var i = 1; i < anchorCount; i++)
            {
                context.Host.ShowAt(context.Anchors[i], MenuNavigationMode.Programmatic);
                await Task.Delay(cycleDelay).ConfigureAwait(true);
            }

            await Task.Delay(DebounceAllowance + DebounceAllowance).ConfigureAwait(true);

            _ = closedCount.Should().Be(0, "Popup should not close while cycling programmatically between root items");
            _ = context.Host.IsOpen.Should().BeTrue("Popup should remain visible after cycling focus programmatically");
            _ = context.Host.Anchor.Should().Be(context.Anchors[anchorCount - 1], "Popup anchor should follow the last programmatic focus");
        }
        finally
        {
            context.Dispose();
        }
    });

    private static async Task WaitForEventAsync(Task task, string failureMessage)
    {
        var completed = await Task.WhenAny(task, Task.Delay(EventTimeout)).ConfigureAwait(true);
        if (completed != task)
        {
            Assert.Fail(failureMessage);
        }

        await task.ConfigureAwait(true);
    }

    private static async Task WaitForRenderCompletionAsync()
    {
        await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
    }

    private static TaskCompletionSource<bool> CreateSignal() =>
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private sealed class PopupMenuHostTestContext : IDisposable
    {
        private PopupMenuHostTestContext(PopupMenuHost host, IReadOnlyList<MenuItemControl> anchors)
        {
            this.Host = host;
            this.Anchors = anchors;
        }

        public PopupMenuHost Host { get; }

        public IReadOnlyList<MenuItemControl> Anchors { get; }

        public static async Task<PopupMenuHostTestContext> CreateAsync(int anchorCount)
        {
            var menuSource = BuildMenuSource(anchorCount);
            var host = new PopupMenuHost
            {
                MenuSource = menuSource,
            };

            var stackPanel = new StackPanel { Orientation = Orientation.Horizontal };
            var anchors = new List<MenuItemControl>(anchorCount);
            for (var i = 0; i < anchorCount; i++)
            {
                var item = menuSource.Items[i];
                var anchor = new MenuItemControl { ItemData = item };
                anchors.Add(anchor);
                stackPanel.Children.Add(anchor);
            }

            await PopupMenuHostTests.LoadTestContentAsync(stackPanel).ConfigureAwait(true);
            await WaitForRenderCompletionAsync().ConfigureAwait(true);

            return new PopupMenuHostTestContext(host, anchors);
        }

        public void Dispose()
        {
            if (this.Host.IsOpen)
            {
                this.Host.Dismiss(MenuDismissKind.Programmatic);
            }

            this.Host.Dispose();
        }

        private static IMenuSource BuildMenuSource(int anchorCount)
        {
            var builder = new MenuBuilder();
            for (var i = 0; i < anchorCount; i++)
            {
                builder.AddMenuItem(new MenuItemData { Text = $"Item {i + 1}" });
            }

            return builder.Build();
        }
    }
}
