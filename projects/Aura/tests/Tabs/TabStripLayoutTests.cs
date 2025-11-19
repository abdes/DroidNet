// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using AwesomeAssertions;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public class TabStripLayoutTests : TabStripTestsBase
{
    public TestContext TestContext { get; set; } = default!;

    [TestMethod]
    public Task LayoutManager_HasDefaultInstance_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);

        // Assert
        _ = tabStrip.LayoutManager.Should().NotBeNull("LayoutManager should have a default instance");
        _ = tabStrip.LayoutManager.Should().BeOfType<TabStripLayoutManager>();
    });

    [TestMethod]
    public Task MaxItemWidth_PassedToLayoutManager_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        const double expectedMaxWidth = 350.0;
        tabStrip.MaxItemWidth = expectedMaxWidth;

        // Assert
        _ = tabStrip.LayoutManager.MaxItemWidth.Should().Be(expectedMaxWidth);
    });

    [TestMethod]
    public Task PreferredItemWidth_PassedToLayoutManager_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        const double expectedPreferredWidth = 180.0;
        tabStrip.PreferredItemWidth = expectedPreferredWidth;

        // Assert
        _ = tabStrip.LayoutManager.PreferredItemWidth.Should().Be(expectedPreferredWidth);
    });

    [TestMethod]
    public Task TabWidthPolicy_PassedToLayoutManager_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        const TabWidthPolicy expectedPolicy = TabWidthPolicy.Equal;
        tabStrip.TabWidthPolicy = expectedPolicy;

        // Assert
        _ = tabStrip.LayoutManager.Policy.Should().Be(expectedPolicy);
    });

    [TestMethod]
    public Task ChangingMaxItemWidth_UpdatesLayoutManager_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        const double newMaxWidth = 400.0;

        // Act
        tabStrip.MaxItemWidth = newMaxWidth;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.LayoutManager.MaxItemWidth.Should().Be(newMaxWidth);
    });

    [TestMethod]
    public Task OverflowButtons_InitiallyHidden_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(2).ConfigureAwait(true);

        // Assert - With only 2 tabs, overflow buttons should be hidden
        var leftButton = tabStrip.GetOverflowLeftButton();
        var rightButton = tabStrip.GetOverflowRightButton();

        if (leftButton != null)
        {
            _ = leftButton.Visibility.Should().Be(Visibility.Collapsed);
        }

        if (rightButton != null)
        {
            _ = rightButton.Visibility.Should().Be(Visibility.Collapsed);
        }
    });

    [TestMethod]
    public Task OverflowButtons_Exist_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(2).ConfigureAwait(true);

        // Assert - Buttons should exist in template
        var leftButton = tabStrip.GetOverflowLeftButton();
        var rightButton = tabStrip.GetOverflowRightButton();

        _ = leftButton.Should().NotBeNull("Left overflow button should exist");
        _ = rightButton.Should().NotBeNull("Right overflow button should exist");
    });

    [TestMethod]
    public Task ScrollHost_Exists_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(2).ConfigureAwait(true);

        // Assert
        var scrollHost = tabStrip.GetScrollHost();
        _ = scrollHost.Should().NotBeNull("ScrollHost should exist");
    });

    [TestMethod]
    public Task LeftOverflowButton_ClickScrollsLeft_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(20).ConfigureAwait(true); // Many tabs to ensure scrolling

        var scrollHost = tabStrip.GetScrollHost();
        _ = scrollHost.Should().NotBeNull();

        // Scroll to the right first
        _ = scrollHost.ChangeView(scrollHost.ScrollableWidth, verticalOffset: null, zoomFactor: null, disableAnimation: true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        var initialOffset = scrollHost.HorizontalOffset;
        _ = initialOffset.Should().BePositive("Should be scrolled to the right");

        var leftButton = tabStrip.GetOverflowLeftButton();
        _ = leftButton.Should().NotBeNull();

        // Act - Simulate clicking left button
        tabStrip.HandleOverflowLeftClick();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = scrollHost.HorizontalOffset.Should().BeLessThan(initialOffset, "Should have scrolled left");
    });

    [TestMethod]
    public Task RightOverflowButton_ClickScrollsRight_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(20).ConfigureAwait(true); // Many tabs to ensure scrolling

        var scrollHost = tabStrip.GetScrollHost();
        _ = scrollHost.Should().NotBeNull();

        // Start at left
        _ = scrollHost.ChangeView(0, verticalOffset: null, zoomFactor: null, disableAnimation: true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        var initialOffset = scrollHost.HorizontalOffset;

        var rightButton = tabStrip.GetOverflowRightButton();
        _ = rightButton.Should().NotBeNull();

        // Act - Simulate clicking right button
        tabStrip.HandleOverflowRightClick();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = scrollHost.HorizontalOffset.Should().BeGreaterThan(initialOffset, "Should have scrolled right");
    });

    [TestMethod]
    public Task AddingManyTabs_MakesContentScrollable_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true); // Many tabs to ensure scrolling
        var scrollHost = tabStrip.GetScrollHost();
        _ = scrollHost.Should().NotBeNull();

        // Act - Add many tabs
        for (var i = 0; i < 25; i++)
        {
            tabStrip.Items.Add(CreateTabItem(string.Create(CultureInfo.InvariantCulture, $"Tab {i + 1}")));
        }

        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = _ = scrollHost.ScrollableWidth.Should().BePositive("With many tabs, content should be scrollable");
    });

    [TestMethod]
    public Task RemovingTabs_UpdatesScrollableWidth_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(25).ConfigureAwait(true);
        var scrollHost = tabStrip.GetScrollHost();
        _ = scrollHost.Should().NotBeNull();
        var initialScrollableWidth = _ = scrollHost.ScrollableWidth;
        _ = initialScrollableWidth.Should().BePositive();

        // Act - Remove most tabs
        while (tabStrip.Items.Count > 3)
        {
            tabStrip.Items.RemoveAt(tabStrip.Items.Count - 1);
        }

        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = scrollHost.ScrollableWidth.Should().BeLessThan(initialScrollableWidth, "Removing tabs should reduce scrollable width");
    });

    [TestMethod]
    public Task ChangingTabWidthPolicy_TriggersLayout_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(5).ConfigureAwait(true);
        tabStrip.TabWidthPolicy = TabWidthPolicy.Auto;
        _ = tabStrip.LayoutManager.Policy.Should().Be(TabWidthPolicy.Auto);

        // Act
        tabStrip.TabWidthPolicy = TabWidthPolicy.Equal;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.LayoutManager.Policy.Should().Be(TabWidthPolicy.Equal, "LayoutManager should receive updated policy");
    });

    [TestMethod]
    public Task PinnedAndRegularRepeaters_BothExist_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(5, pinnedCount: 2).ConfigureAwait(true);

        // Assert
        var pinnedRepeater = tabStrip.GetPinnedRepeater();
        var regularRepeater = tabStrip.GetRegularRepeater();

        _ = pinnedRepeater.Should().NotBeNull("Pinned repeater should exist");
        _ = regularRepeater.Should().NotBeNull("Regular repeater should exist");
    });
}
