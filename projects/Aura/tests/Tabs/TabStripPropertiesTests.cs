// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Drag;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public class TabStripPropertiesTests : TabStripTestsBase
{
    public TestContext TestContext { get; set; } = default!;

    [TestMethod]
    public Task Items_IsObservableCollection_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.Items.Should().BeOfType<ObservableCollection<TabItem>>("Items should be an ObservableCollection");
    });

    [TestMethod]
    public Task Items_NeverNull_Async() => EnqueueAsync(() =>
    {
        // Arrange & Act
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.Items.Should().NotBeNull("Items should never be null");
    });

    [TestMethod]
    public Task PinnedItemsView_IsReadOnly_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.PinnedItemsView.Should().BeAssignableTo<IReadOnlyList<TabItem>>();
    });

    [TestMethod]
    public Task RegularItemsView_IsReadOnly_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.RegularItemsView.Should().BeAssignableTo<IReadOnlyList<TabItem>>();
    });

    [TestMethod]
    public Task MaxItemWidth_HasDefaultValue_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.MaxItemWidth.Should().BePositive("MaxItemWidth should have a positive default value");
    });

    [TestMethod]
    public Task MaxItemWidth_CanBeChanged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);
        const double newMaxWidth = 500.0;

        // Act
        tabStrip.MaxItemWidth = newMaxWidth;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.MaxItemWidth.Should().Be(newMaxWidth, "MaxItemWidth should be settable");
    });

    [TestMethod]
    public Task PreferredItemWidth_HasDefaultValue_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.PreferredItemWidth.Should().BePositive("PreferredItemWidth should have a positive default value");
    });

    [TestMethod]
    public Task PreferredItemWidth_CanBeChanged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);
        const double newPreferredWidth = 200.0;

        // Act
        tabStrip.PreferredItemWidth = newPreferredWidth;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.PreferredItemWidth.Should().Be(newPreferredWidth, "PreferredItemWidth should be settable");
    });

    [TestMethod]
    public Task TabWidthPolicy_HasDefaultValue_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.TabWidthPolicy.Should().Be(TabWidthPolicy.Auto, "TabWidthPolicy should default to Auto");
    });

    [TestMethod]
    public Task TabWidthPolicy_CanBeChanged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);
        const TabWidthPolicy newPolicy = TabWidthPolicy.Equal;

        // Act
        tabStrip.TabWidthPolicy = newPolicy;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.TabWidthPolicy.Should().Be(newPolicy, "TabWidthPolicy should be settable");
    });

    [TestMethod]
    public Task DragCoordinator_InitiallyNull_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.DragCoordinator.Should().BeNull("DragCoordinator should be null by default");
    });

    [TestMethod]
    public Task DragCoordinator_CanBeSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);
        var coordinator = new TestCoordinator();

        // Act
        tabStrip.DragCoordinator = coordinator;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.DragCoordinator.Should().Be(coordinator, "DragCoordinator should be settable");
    });

    [TestMethod]
    public Task DragThreshold_HasDefaultValue_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.DragThreshold.Should().BePositive("DragThreshold should have a positive default value");
    });

    [TestMethod]
    public Task DragThreshold_CanBeChanged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);
        const double newThreshold = 15.0;

        // Act
        tabStrip.DragThreshold = newThreshold;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.DragThreshold.Should().Be(newThreshold, "DragThreshold should be settable");
    });

    [TestMethod]
    public Task SelectedItem_InitiallyNull_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.SelectedItem.Should().BeNull("No item should be selected initially");
    });

    [TestMethod]
    public Task SelectedItem_CanBeSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var itemToSelect = tabStrip.Items[1];

        // Act
        tabStrip.SelectedItem = itemToSelect;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(itemToSelect);
    });

    [TestMethod]
    public Task SelectedIndex_InitiallyNegativeOne_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.SelectedIndex.Should().Be(-1, "SelectedIndex should be -1 when nothing is selected");
    });

    [TestMethod]
    public Task SelectedIndex_CanBeSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        // Act
        tabStrip.SelectedIndex = 2;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedIndex.Should().Be(2);
        _ = tabStrip.SelectedItem.Should().Be(tabStrip.Items[2]);
    });

    [TestMethod]
    public Task LayoutManager_HasDefaultInstance_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Assert
        _ = tabStrip.LayoutManager.Should().NotBeNull("LayoutManager should have a default instance");
        _ = tabStrip.LayoutManager.Should().BeOfType<TabStripLayoutManager>();
    });

    [TestMethod]
    public Task LayoutManager_CanBeReplaced_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);
        var customLayoutManager = new TabStripLayoutManager();

        // Act
        tabStrip.LayoutManager = customLayoutManager;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.LayoutManager.Should().Be(customLayoutManager, "LayoutManager should be replaceable");
    });

    // Minimal test coordinator implementation to avoid depending on concrete TabDragCoordinator ctor.
    private sealed class TestCoordinator : ITabDragCoordinator
    {
        public void StartDrag(IDragPayload item, int tabIndex, ITabStrip source, FrameworkElement stripContainer, FrameworkElement draggedElement, DroidNet.Coordinates.SpatialPoint<DroidNet.Coordinates.ElementSpace> initialPosition, Windows.Foundation.Point hotspotOffsets)
        {
            // No-op for tests
        }

        public void EndDrag(DroidNet.Coordinates.SpatialPoint<DroidNet.Coordinates.ScreenSpace> screenPoint)
        {
        }

        public void Abort()
        {
        }

        public void RegisterTabStrip(ITabStrip strip)
        {
        }

        public void UnregisterTabStrip(ITabStrip strip)
        {
        }
    }
}
