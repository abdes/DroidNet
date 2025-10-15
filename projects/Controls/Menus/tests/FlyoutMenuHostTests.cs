// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml.Controls;
using MenuItemControl = DroidNet.Controls.Menus.MenuItem;

namespace DroidNet.Controls.Menus.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("FlyoutMenuHostTests")]
[TestCategory("UITest")]
public sealed partial class FlyoutMenuHostTests : VisualUserInterfaceTests
{
    private static readonly TimeSpan HoverCycleDelay = TimeSpan.FromMilliseconds(20);

    public TestContext TestContext { get; set; }

    [TestMethod]
    public Task ShowAtRaisesOpeningAndOpened_Async() => EnqueueAsync(async () =>
    {
        using var harness = await FlyoutMenuHostHarness.CreateAsync().ConfigureAwait(true);

        var openingCount = 0;
        var openedSignal = CreateSignal();

        harness.Host.Opening += (_, _) => openingCount++;
        harness.Host.Opened += (_, _) => openedSignal.TrySetResult(true);

        ConfigureHostForRoot(harness, 0);
        harness.Host.ShowAt(harness.Anchors[0], MenuNavigationMode.PointerInput);

        await WaitForEventAsync(openedSignal.Task, "Flyout should open").ConfigureAwait(true);
        await WaitForRenderCompletionAsync().ConfigureAwait(true);

        _ = openingCount.Should().Be(1);
        _ = harness.Host.IsOpen.Should().BeTrue();
        _ = harness.Host.Anchor.Should().Be(harness.Anchors[0]);
        _ = harness.Host.MenuSource!.Items.Should().Equal(harness.RootItems[0].SubItems);
    });

    [TestMethod]
    public Task DismissRaisesClosingAndClosed_Async() => EnqueueAsync(async () =>
    {
        using var harness = await FlyoutMenuHostHarness.CreateAsync().ConfigureAwait(true);

        ConfigureHostForRoot(harness, 0);

        var openedSignal = CreateSignal();
        harness.Host.Opened += (_, _) => openedSignal.TrySetResult(true);

        harness.Host.ShowAt(harness.Anchors[0], MenuNavigationMode.PointerInput);
        await WaitForEventAsync(openedSignal.Task, "Flyout should open").ConfigureAwait(true);

        var closingSignal = new TaskCompletionSource<MenuDismissKind>(TaskCreationOptions.RunContinuationsAsynchronously);
        var closedSignal = CreateSignal();

        harness.Host.Closing += (_, args) => closingSignal.TrySetResult(args.Kind);
        harness.Host.Closed += (_, _) => closedSignal.TrySetResult(true);

        harness.Host.Dismiss(MenuDismissKind.PointerInput);

        var kind = await closingSignal.Task.ConfigureAwait(true);
        await WaitForEventAsync(closedSignal.Task, "Flyout should close").ConfigureAwait(true);

        _ = kind.Should().Be(MenuDismissKind.PointerInput);
        _ = harness.Host.IsOpen.Should().BeFalse();
    });

    [TestMethod]
    public Task DismissCanBeCancelled_Async() => EnqueueAsync(async () =>
    {
        using var harness = await FlyoutMenuHostHarness.CreateAsync().ConfigureAwait(true);

        ConfigureHostForRoot(harness, 0);
        var openedSignal = CreateSignal();
        harness.Host.Opened += (_, _) => openedSignal.TrySetResult(true);

        harness.Host.ShowAt(harness.Anchors[0], MenuNavigationMode.PointerInput);
        await WaitForEventAsync(openedSignal.Task, "Flyout should open").ConfigureAwait(true);

        var closingCount = 0;
        var closed = false;

        harness.Host.Closing += (_, args) =>
        {
            closingCount++;
            args.Cancel = true;
        };

        harness.Host.Closed += (_, _) => closed = true;

        harness.Host.Dismiss(MenuDismissKind.Programmatic);
        await Task.Delay(HoverCycleDelay, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = closingCount.Should().Be(1);
        _ = closed.Should().BeFalse();
        _ = harness.Host.IsOpen.Should().BeTrue();
    });

    [TestMethod]
    public Task ShowAtWhileOpenReanchorsWithoutClosing_Async() => EnqueueAsync(async () =>
    {
        using var harness = await FlyoutMenuHostHarness.CreateAsync().ConfigureAwait(true);

        ConfigureHostForRoot(harness, 0);

        var firstOpened = CreateSignal();
        harness.Host.Opened += (_, _) => firstOpened.TrySetResult(true);

        harness.Host.ShowAt(harness.Anchors[0], MenuNavigationMode.PointerInput);
        await WaitForEventAsync(firstOpened.Task, "Initial open should complete").ConfigureAwait(true);

        var reopenedSignal = CreateSignal();
        harness.Host.Opened += (_, _) => reopenedSignal.TrySetResult(true);

        ConfigureHostForRoot(harness, 1);
        harness.Host.ShowAt(harness.Anchors[1], MenuNavigationMode.PointerInput);

        // Wait for the reanchoring to complete (ShowAt on an open flyout closes then reopens asynchronously)
        await WaitForEventAsync(reopenedSignal.Task, "Flyout should reopen after reanchoring").ConfigureAwait(true);

        _ = harness.Host.IsOpen.Should().BeTrue("Flyout should be open after reanchoring");
        _ = harness.Host.Anchor.Should().Be(harness.Anchors[1]);
        _ = harness.Host.MenuSource!.Items.Should().Equal(harness.RootItems[1].SubItems);
    });

    [TestMethod]
    public Task RapidHoverReanchorKeepsLatestAnchor_Async() => EnqueueAsync(async () =>
    {
        using var harness = await FlyoutMenuHostHarness.CreateAsync(rootCount: 5, submenuCount: 3).ConfigureAwait(true);

        ConfigureHostForRoot(harness, 0);
        var openedSignal = CreateSignal();
        harness.Host.Opened += (_, _) => openedSignal.TrySetResult(true);

        harness.Host.ShowAt(harness.Anchors[0], MenuNavigationMode.PointerInput);
        await WaitForEventAsync(openedSignal.Task, "Initial open should succeed").ConfigureAwait(true);

        var openingAnchors = new List<MenuItemControl>();
        TaskCompletionSource<bool>? lastOpenedSignal = null;

        harness.Host.Opening += (_, _) => openingAnchors.Add((MenuItemControl)harness.Host.Anchor!);
        harness.Host.Opened += (_, _) => lastOpenedSignal?.TrySetResult(true);

        var lastAnchorIndex = 0;
        for (var i = 0; i < 20; i++)
        {
            lastAnchorIndex = (i % (harness.Anchors.Count - 1)) + 1;
            ConfigureHostForRoot(harness, lastAnchorIndex);

            // Create a new signal for each ShowAt to track when it completes
            lastOpenedSignal = CreateSignal();
            harness.Host.ShowAt(harness.Anchors[lastAnchorIndex], MenuNavigationMode.PointerInput);
            await Task.Delay(HoverCycleDelay, this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        // Wait for the final ShowAt to complete opening
        await WaitForEventAsync(lastOpenedSignal!.Task, "Final flyout should open").ConfigureAwait(true);

        _ = harness.Host.IsOpen.Should().BeTrue();
        _ = harness.Host.Anchor.Should().Be(harness.Anchors[lastAnchorIndex]);
        _ = harness.Host.MenuSource!.Items.Should().Equal(harness.RootItems[lastAnchorIndex].SubItems);
        _ = openingAnchors.Should().NotBeEmpty();
    });

    private static async Task WaitForRenderCompletionAsync()
        => _ = await CompositionTargetHelper
                .ExecuteAfterCompositionRenderingAsync(static () => { })
                .ConfigureAwait(true);

    private static async Task WaitForEventAsync(Task task, string failureMessage)
    {
        var completed = await Task.WhenAny(task, Task.Delay(TimeSpan.FromSeconds(2))).ConfigureAwait(true);
        if (completed != task)
        {
            Assert.Fail(failureMessage);
        }

        await task.ConfigureAwait(true);
    }

    private static TaskCompletionSource<bool> CreateSignal() =>
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private static void ConfigureHostForRoot(FlyoutMenuHostHarness harness, int rootIndex)
    {
        for (var i = 0; i < harness.RootItems.Count; i++)
        {
            harness.RootItems[i].IsExpanded = i == rootIndex;
        }

        var root = harness.RootItems[rootIndex];
        harness.Host.MenuSource = new MenuSourceView(root.SubItems, harness.MenuSource.Services);
    }

    private sealed partial class FlyoutMenuHostHarness : IDisposable
    {
        private FlyoutMenuHostHarness(FlyoutMenuHost host, IMenuSource menuSource, IReadOnlyList<MenuItemControl> anchors)
        {
            this.Host = host;
            this.MenuSource = menuSource;
            this.Anchors = anchors;
        }

        public FlyoutMenuHost Host { get; }

        public IMenuSource MenuSource { get; }

        public IReadOnlyList<MenuItemControl> Anchors { get; }

        public ObservableCollection<MenuItemData> RootItems => this.MenuSource.Items;

        public static async Task<FlyoutMenuHostHarness> CreateAsync(int rootCount = 3, int submenuCount = 2)
        {
            var menuSource = BuildMenuSource(rootCount, submenuCount);

            var stackPanel = new StackPanel { Orientation = Orientation.Horizontal };
            var anchors = new List<MenuItemControl>(rootCount);

            foreach (var item in menuSource.Items)
            {
                var anchor = new MenuItemControl { ItemData = item, ShowSubmenuGlyph = false };
                anchors.Add(anchor);
                stackPanel.Children.Add(anchor);
            }

            await FlyoutMenuHostTests.LoadTestContentAsync(stackPanel).ConfigureAwait(true);
            await WaitForRenderCompletionAsync().ConfigureAwait(true);

            var host = new FlyoutMenuHost { RootSurface = new StubRootSurface(menuSource.Items) };
            return new FlyoutMenuHostHarness(host, menuSource, anchors);
        }

        public void Dispose()
        {
            if (this.Host.IsOpen)
            {
                this.Host.Dismiss(MenuDismissKind.Programmatic);
            }

            this.Host.Dispose();
        }

        private static IMenuSource BuildMenuSource(int rootCount, int submenuCount)
        {
            var builder = new MenuBuilder();
            for (var i = 0; i < rootCount; i++)
            {
                var children = Enumerable.Range(0, submenuCount)
                    .Select(j => new MenuItemData
                    {
                        Text = string.Format(System.Globalization.CultureInfo.InvariantCulture, "Item {0}.{1}", i + 1, j + 1),
                    }).ToList();

                _ = builder.AddMenuItem(new MenuItemData
                {
                    Text = string.Format(System.Globalization.CultureInfo.InvariantCulture, "Root {0}", i + 1),
                    SubItems = children,
                });
            }

            return builder.Build();
        }

        private sealed class StubRootSurface(IList<MenuItemData> items) : IRootMenuSurface
        {
            private readonly IList<MenuItemData> items = items;

            public object? FocusElement => null;

            public bool Show(MenuNavigationMode navigationMode) => true;

            public void Dismiss(MenuDismissKind kind = MenuDismissKind.Programmatic)
            {
            }

            public MenuItemData GetAdjacentItem(MenuItemData itemData, MenuNavigationDirection direction, bool wrap = true)
            {
                var index = this.items.IndexOf(itemData);
                if (index < 0)
                {
                    throw new ArgumentException("Item is not part of the root surface", nameof(itemData));
                }

                var step = direction is MenuNavigationDirection.Right or MenuNavigationDirection.Down ? 1 : -1;
                var count = this.items.Count;
                return wrap
                    ? this.items[(index + step + count) % count]
                    : this.items[Math.Clamp(index + step, 0, count - 1)];
            }

            public MenuItemData? GetExpandedItem() => this.items.FirstOrDefault(static item => item.IsExpanded);

            public MenuItemData? GetFocusedItem() => null;

            public bool FocusItem(MenuItemData itemData, MenuNavigationMode navigationMode) => this.items.Contains(itemData);

            public bool FocusFirstItem(MenuNavigationMode navigationMode) => this.items.Any();

            public bool ExpandItem(MenuItemData itemData, MenuNavigationMode navigationMode)
            {
                foreach (var item in this.items)
                {
                    item.IsExpanded = ReferenceEquals(item, itemData);
                }

                return true;
            }

            public void CollapseItem(MenuItemData itemData, MenuNavigationMode navigationMode) => itemData.IsExpanded = false;
        }
    }
}
