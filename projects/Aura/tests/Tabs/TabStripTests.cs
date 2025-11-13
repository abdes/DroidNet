// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using DroidNet.TestHelpers;
using AwesomeAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Aura.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
[TestCategory("TabStrip.Core")]
public class TabStripTests : TabStripTestsBase
{
    public TestContext TestContext { get; set; } = default!;

    [TestMethod]
    public Task SetsTemplatePartsCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 1).ConfigureAwait(true);

        // Act

        // Assert - Verify all required template parts are present
        var rootGrid = tabStrip.GetRootGrid();
        _ = rootGrid.Should().NotBeNull("Root grid template part should be present");

        var pinnedRepeater = tabStrip.GetPinnedRepeater();
        _ = pinnedRepeater.Should().NotBeNull("Pinned items repeater should be present");
        _ = pinnedRepeater.Should().BeOfType<ItemsRepeater>();

        var regularRepeater = tabStrip.GetRegularRepeater();
        _ = regularRepeater.Should().NotBeNull("Regular items repeater should be present");
        _ = regularRepeater.Should().BeOfType<ItemsRepeater>();

        var scrollHost = tabStrip.GetScrollHost();
        _ = scrollHost.Should().NotBeNull("Scroll host should be present");
        _ = scrollHost.Should().BeOfType<ScrollViewer>();

        var leftButton = tabStrip.GetOverflowLeftButton();
        _ = leftButton.Should().NotBeNull("Left overflow button should be present");
        _ = leftButton.Should().BeOfType<RepeatButton>();

        var rightButton = tabStrip.GetOverflowRightButton();
        _ = rightButton.Should().NotBeNull("Right overflow button should be present");
        _ = rightButton.Should().BeOfType<RepeatButton>();
    });

    [TestMethod]
    public Task RootGrid_IsPresent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = new TestableTabStrip();

        // Act
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        // Assert
        var rootGrid = tabStrip.GetRootGrid();
        _ = rootGrid.Should().NotBeNull("Root grid is essential for the control");
        _ = rootGrid!.Name.Should().Be(TabStrip.RootGridPartName);
    });

    [TestMethod]
    public Task DefaultState_IsValid_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().NotBeNull("Items collection should be initialized");
        _ = tabStrip.Items.Should().BeEmpty("Items collection should start empty");
        _ = tabStrip.SelectedItem.Should().BeNull("No item should be selected initially");
        _ = tabStrip.SelectedIndex.Should().Be(-1, "SelectedIndex should be -1 when nothing is selected");
        _ = tabStrip.PinnedItemsView.Should().NotBeNull("PinnedItemsView should be initialized");
        _ = tabStrip.RegularItemsView.Should().NotBeNull("RegularItemsView should be initialized");
        _ = tabStrip.DragCoordinator.Should().BeNull("DragCoordinator should be null initially");
    });

    [TestMethod]
    public Task Items_CollectionIsNotNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);

        // Act

        // Assert
        _ = tabStrip.Items.Should().NotBeNull("Items should never be null");
        _ = tabStrip.Items.Should().BeAssignableTo<System.Collections.ObjectModel.ObservableCollection<TabItem>>();
    });

    [TestMethod]
    public Task PinnedAndRegularViews_AreNotNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);

        // Act

        // Assert
        _ = tabStrip.PinnedItemsView.Should().NotBeNull("PinnedItemsView should be initialized");
        _ = tabStrip.RegularItemsView.Should().NotBeNull("RegularItemsView should be initialized");
        _ = tabStrip.PinnedItemsView.Should().BeEmpty("Should start with no pinned items");
        _ = tabStrip.RegularItemsView.Should().BeEmpty("Should start with no regular items");
    });

    [TestMethod]
    public Task LoggerFactory_Provided_EmitsLogs_And_PropagatesToChildren_Async() => EnqueueAsync(async () =>
    {
        // Arrange - create a TestLoggerProvider-backed factory and assign to the control
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);
        var testProvider = new TestLoggerProvider();
        using var factory = Microsoft.Extensions.Logging.LoggerFactory.Create(builder => builder.AddProvider(testProvider).SetMinimumLevel(LogLevel.Trace));
        tabStrip.LoggerFactory = factory;

        // Act - load the control and add an item so child containers are created
        var item = CreateTabItem("Logging Tab");
        tabStrip.Items.Add(item);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - the provider should have received at least one log entry
        _ = testProvider.Messages.Should().NotBeEmpty("setting LoggerFactory should enable logging from the control or its children");

        // And the created child TabStripItem should have the factory propagated
        var child = tabStrip.GetTabStripItemForIndex(0);
        _ = child.Should().NotBeNull("Child container should exist after adding an item");

        var loggerFactoryProp = child.GetType().GetProperty("LoggerFactory", BindingFlags.Public | BindingFlags.Instance | BindingFlags.NonPublic);
        _ = loggerFactoryProp.Should().NotBeNull("TabStripItem should expose a LoggerFactory property");

        var childFactory = loggerFactoryProp!.GetValue(child);
        _ = childFactory.Should().Be(factory, "LoggerFactory should be propagated from the TabStrip to its children");
    });

    [TestMethod]
    public Task AddItem_AppearsInItemsCollection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);

        // Act
        var item = CreateTabItem("New Tab");
        tabStrip.Items.Add(item);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().Contain(item, "Added item should be in Items collection");
        _ = tabStrip.Items.Count.Should().Be(1, "Items count should increase");
    });

    [TestMethod]
    public Task RemoveItem_DisappearsFromCollection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 0).ConfigureAwait(true);
        var itemToRemove = tabStrip.Items[1];

        // Act
        _ = tabStrip.Items.Remove(itemToRemove);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().NotContain(itemToRemove, "Removed item should not be in collection");
        _ = tabStrip.Items.Count.Should().Be(2, "Items count should decrease");
    });

    [TestMethod]
    public Task ClearItems_EmptiesCollection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(5, pinnedCount: 0).ConfigureAwait(true);

        // Act
        tabStrip.Items.Clear();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().BeEmpty("Clear should remove all items");
        _ = tabStrip.PinnedItemsView.Should().BeEmpty("Pinned view should be empty");
        _ = tabStrip.RegularItemsView.Should().BeEmpty("Regular view should be empty");
    });

    [TestMethod]
    public Task InsertItem_AtSpecificIndex_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 0).ConfigureAwait(true);
        var newItem = CreateTabItem("Inserted Tab");

        // Act
        tabStrip.Items.Insert(1, newItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items[1].Should().Be(newItem, "Item should be at the specified index");
        _ = tabStrip.Items.Count.Should().Be(4, "Total count should increase");
    });

    [TestMethod]
    public Task ReplaceItem_UpdatesCollection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 0).ConfigureAwait(true);
        var oldItem = tabStrip.Items[1];
        var newItem = CreateTabItem("Replacement Tab");

        // Act
        tabStrip.Items[1] = newItem;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items[1].Should().Be(newItem, "Item should be replaced");
        _ = tabStrip.Items.Should().NotContain(oldItem, "Old item should not be in collection");
        _ = tabStrip.Items.Count.Should().Be(3, "Total count should remain the same");
    });

    [TestMethod]
    public Task EmptyCollection_HasCorrectState_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().BeEmpty();
        _ = tabStrip.SelectedItem.Should().BeNull();
        _ = tabStrip.SelectedIndex.Should().Be(-1);
        _ = tabStrip.PinnedItemsView.Should().BeEmpty();
        _ = tabStrip.RegularItemsView.Should().BeEmpty();
    });

    [TestMethod]
    public Task AddFirstItem_ToEmptyCollection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);

        // Act
        var firstItem = CreateTabItem("First Tab");
        tabStrip.Items.Add(firstItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Count.Should().Be(1);
        _ = tabStrip.Items[0].Should().Be(firstItem);
    });
}
