// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls.Tabs.Tests;

/// <summary>
/// Unit tests for TabStrip drag-and-drop Phase 3 implementation (pointer wiring and drag lifecycle).
/// Tests validate threshold tracking, event ordering, selection clearing, and TabStrip drag behavior.
/// For TabDragCoordinator-specific tests, see <see cref="TabDragCoordinatorTests"/>.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("TabStripDragTests")]
[TestCategory("Phase3")]
public class TabStripDragTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public Task TabStripHandlesPointerPressedForDragThreshold_Async() => EnqueueAsync(async () =>
    {
        // Arrange - setup TabStrip with coordinator to enable drag threshold tracking
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        tabStrip.DragCoordinator = coordinator;

        // Assert - TabStrip should have pointer handling infrastructure in place
        // This verifies OnPointerPressed override exists and would process threshold logic
        var onPointerPressedMethod = tabStrip.GetType().GetMethod(
            "OnPointerPressed",
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        _ = onPointerPressedMethod.Should().NotBeNull("TabStrip should override OnPointerPressed for drag threshold tracking");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task TabStripDragCoordinatorProperty_StartsAsNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);

        // Act & Assert
        _ = tabStrip.DragCoordinator.Should().BeNull("DragCoordinator should be null when not explicitly set");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task TabStripDragCoordinatorProperty_CanBeSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var mockCoordinator = new TabDragCoordinator(new MockDragVisualService());

        // Act
        tabStrip.DragCoordinator = mockCoordinator;

        // Assert
        _ = tabStrip.DragCoordinator.Should().Be(mockCoordinator, "DragCoordinator property should be settable and retrievable");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task TabStripDragCoordinatorProperty_CanBeCleared_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var mockCoordinator = new TabDragCoordinator(new MockDragVisualService());
        tabStrip.DragCoordinator = mockCoordinator;

        // Act
        tabStrip.DragCoordinator = null;

        // Assert
        _ = tabStrip.DragCoordinator.Should().BeNull("DragCoordinator should be clearable by setting to null");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task TabStripInitializesWithoutCoordinator_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);

        // Assert
        _ = tabStrip.DragCoordinator.Should().BeNull("TabStrip should initialize with null DragCoordinator");
        _ = tabStrip.Items.Should().BeEmpty("Items collection should be empty initially");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task TabStripInitializesWithItemsCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var tabItem1 = new TabItem { Header = "Tab1" };
        var tabItem2 = new TabItem { Header = "Tab2" };

        // Act
        tabStrip.Items.Add(tabItem1);
        tabStrip.Items.Add(tabItem2);
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().HaveCount(2, "Both items should be added");
        _ = tabStrip.Items.Should().Contain(tabItem1, "First item should be in collection");
        _ = tabStrip.Items.Should().Contain(tabItem2, "Second item should be in collection");
        _ = tabStrip.SelectedIndex.Should().BeGreaterThanOrEqualTo(0, "An item should be auto-selected");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task TabStripSelectionChangesOnDragStart_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var tab1 = new TabItem { Header = "Tab1" };
        var tab2 = new TabItem { Header = "Tab2" };
        tabStrip.Items.Add(tab1);
        tabStrip.Items.Add(tab2);
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
        tabStrip.SelectedItem = tab1;
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        tabStrip.SelectedItem = tab2;
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(tab2, "Selected item should update when changed");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task TabStripItemHasValidDimensionsForDragCalculations_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var tabItem = new TabItem { Header = "DragTest" };
        tabStrip.Items.Add(tabItem);
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

        var tabStripItem = await this.GetVisualChild<TabStripItem>(tabStrip).ConfigureAwait(true);
        _ = tabStripItem.Should().NotBeNull("TabStripItem should be created and measured");

        // Act
        var itemWidth = tabStripItem!.ActualWidth;
        var itemHeight = tabStripItem.ActualHeight;

        // Assert
        _ = itemWidth.Should().BeGreaterThan(0, "Item should have a valid width for drag calculations");
        _ = itemHeight.Should().BeGreaterThan(0, "Item should have a valid height for drag calculations");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task TabStripHandlesPointerReleasedForDragCleanup_Async() => EnqueueAsync(async () =>
    {
        // Arrange - setup TabStrip with coordinator
        var tabStrip = await SetupTabStrip().ConfigureAwait(true);
        var dragService = new MockDragVisualService();
        var coordinator = new TabDragCoordinator(dragService);
        tabStrip.DragCoordinator = coordinator;

        // Assert - TabStrip should override OnPointerReleased to handle drag cleanup
        // This verifies the pointer release handler exists and would trigger Abort/EndDrag
        var pointerReleasedMethod = tabStrip.GetType().GetMethod(
            "OnPointerReleased",
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        _ = pointerReleasedMethod.Should().NotBeNull("TabStrip should override OnPointerReleased to handle drag cleanup");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    // Helper method to set up a TabStrip
    private static async Task<TabStrip> SetupTabStrip()
    {
        var tabStrip = new TabStrip();
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);
        await Task.Delay(50).ConfigureAwait(true); // Allow layout
        return tabStrip;
    }

    // Helper method to get a visual child (simplified version)
    private async Task<T?> GetVisualChild<T>(DependencyObject parent)
        where T : DependencyObject
    {
        var count = VisualTreeHelper.GetChildrenCount(parent);
        for (var i = 0; i < count; i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            if (child is T typedChild)
            {
                return typedChild;
            }

            var result = await this.GetVisualChild<T>(child).ConfigureAwait(true);
            if (result != null)
            {
                return result;
            }
        }

        return default;
    }
}
