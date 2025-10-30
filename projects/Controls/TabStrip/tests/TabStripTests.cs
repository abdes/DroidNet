// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls.Tabs.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("TabStripTests")]
[TestCategory("UITest")]
public class TabStripTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task InitializesItemsCollectionCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().NotBeNull();
        _ = tabStrip.Items.Should().BeEmpty();
        _ = tabStrip.PinnedItemsView.Should().NotBeNull();
        _ = tabStrip.RegularItemsView.Should().NotBeNull();
    });

    [TestMethod]
    public Task SetsTemplatePartsCorrectly_Async() => EnqueueAsync(async () =>
    {
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);

        // Assert - Verify all required template parts are present
        CheckPartIsThere(tabStrip, "PartRootGrid");
        CheckPartIsThere(tabStrip, "PartOverflowLeftButton");
        CheckPartIsThere(tabStrip, "PartOverflowRightButton");
        CheckPartIsThere(tabStrip, "PartPinnedItemsRepeater");
        CheckPartIsThere(tabStrip, "PartRegularItemsRepeater");
        CheckPartIsThere(tabStrip, "PartScrollHost");
    });

    [TestMethod]
    public Task AddsItemsToRegularView_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var tabItem = new TabItem { Header = "Test Tab" };

        // Act
        tabStrip.Items.Add(tabItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.RegularItemsView.Should().Contain(tabItem);
        _ = tabStrip.PinnedItemsView.Should().NotContain(tabItem);
    });

    [TestMethod]
    public Task AddsPinnedItemsToPinnedView_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var tabItem = new TabItem { Header = "Pinned Tab", IsPinned = true };

        // Act
        tabStrip.Items.Add(tabItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.PinnedItemsView.Should().Contain(tabItem);
        _ = tabStrip.RegularItemsView.Should().NotContain(tabItem);
    });

    [TestMethod]
    public Task MovesItemFromRegularToPinned_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var tabItem = new TabItem { Header = "Test Tab" };
        tabStrip.Items.Add(tabItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        tabItem.IsPinned = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.PinnedItemsView.Should().Contain(tabItem);
        _ = tabStrip.RegularItemsView.Should().NotContain(tabItem);
    });

    [TestMethod]
    public Task MovesItemFromPinnedToRegular_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var tabItem = new TabItem { Header = "Pinned Tab", IsPinned = true };
        tabStrip.Items.Add(tabItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        tabItem.IsPinned = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.RegularItemsView.Should().Contain(tabItem);
        _ = tabStrip.PinnedItemsView.Should().NotContain(tabItem);
    });

    [TestMethod]
    public Task SetsSelectedItemCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var tabItem1 = new TabItem { Header = "Tab 1" };
        var tabItem2 = new TabItem { Header = "Tab 2" };
        tabStrip.Items.Add(tabItem1);
        tabStrip.Items.Add(tabItem2);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        tabStrip.SelectedItem = tabItem2;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(tabItem2);
        _ = tabStrip.SelectedIndex.Should().Be(1);
        _ = tabItem2.IsSelected.Should().BeTrue();
        _ = tabItem1.IsSelected.Should().BeFalse();
    });

    [TestMethod]
    public Task SetsSelectedIndexCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var tabItem1 = new TabItem { Header = "Tab 1" };
        var tabItem2 = new TabItem { Header = "Tab 2" };
        tabStrip.Items.Add(tabItem1);
        tabStrip.Items.Add(tabItem2);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        tabStrip.SelectedIndex = 1;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(tabItem2);
        _ = tabStrip.SelectedIndex.Should().Be(1);
        _ = tabItem2.IsSelected.Should().BeTrue();
        _ = tabItem1.IsSelected.Should().BeFalse();
    });

    [TestMethod]
    public Task SetsTabWidthPolicyCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);

        // Act
        tabStrip.TabWidthPolicy = TabWidthPolicy.Equal;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.TabWidthPolicy.Should().Be(TabWidthPolicy.Equal);
    });

    [TestMethod]
    public Task SetsMaxItemWidthCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);

        // Act
        tabStrip.MaxItemWidth = 300;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.MaxItemWidth.Should().Be(300);
    });

    [TestMethod]
    public Task SetsPreferredItemWidthCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);

        // Act
        tabStrip.PreferredItemWidth = 200;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.PreferredItemWidth.Should().Be(200);
    });

    [TestMethod]
    public Task SelectsNewlyAddedItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        TabSelectionChangedEventArgs? eventArgs = null;
        tabStrip.SelectionChanged += (s, e) => eventArgs = e;
        var tabItem1 = new TabItem { Header = "Tab 1" };
        var tabItem2 = new TabItem { Header = "Tab 2" };

        // Act
        tabStrip.Items.Add(tabItem1);
        await WaitForRenderCompletion().ConfigureAwait(continueOnCapturedContext: true);

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(tabItem1);
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.NewItem.Should().Be(tabItem1);
        _ = eventArgs.OldItem.Should().BeNull();

        // Act
        tabStrip.Items.Add(tabItem2);
        await WaitForRenderCompletion().ConfigureAwait(true); // Allow dispatcher to process deferred selection change

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(tabItem2);
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.NewItem.Should().Be(tabItem2);
        _ = eventArgs.OldItem.Should().Be(tabItem1);
    });

    internal static async Task<TestableTabStrip> SetupTabStrip()
    {
        var tabStrip = new TestableTabStrip();

        // Ensure the control has a usable ILoggerFactory for tests. Use a lightweight
        // Microsoft.Extensions.Logging factory so CreateLogger(...) returns a real
        // ILogger instance. We keep it minimal to avoid noisy output in test runs.
        var loggerFactory = LoggerFactory.Create(builder => builder
            .SetMinimumLevel(LogLevel.Debug)
            .AddDebug());
        tabStrip.LoggerFactory = loggerFactory;

        // Load the control
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        return tabStrip;
    }

    protected static async Task WaitForRenderCompletion() =>
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { })
            .ConfigureAwait(true);

    protected static void CheckPartIsThere(TabStrip tabStrip, string partName)
    {
        var part = tabStrip.FindDescendant<FrameworkElement>(e => string.Equals(e.Name, partName, StringComparison.Ordinal));
        _ = part.Should().NotBeNull($"Template part '{partName}' should be present");
    }
}
