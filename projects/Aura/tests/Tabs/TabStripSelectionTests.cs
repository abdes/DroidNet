// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.Aura.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
[TestCategory("TabStrip.Selection")]
public class TabStripSelectionTests : TabStripTestsBase
{
    public TestContext TestContext { get; set; } = default!;

    [TestMethod]
    public Task SelectItem_UpdatesSelectedItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var itemToSelect = tabStrip.Items[1];

        // Act
        tabStrip.SelectedItem = itemToSelect;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(itemToSelect, "SelectedItem should be updated");
    });

    [TestMethod]
    public Task SelectItem_UpdatesSelectedIndex_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        // Act
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedIndex.Should().Be(1, "SelectedIndex should reflect the selected item's position");
    });

    [TestMethod]
    public Task SelectItem_DeselectsPrevious_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        // Select first item
        tabStrip.Items[0].IsSelected = true;
        tabStrip.SelectedItem = tabStrip.Items[0];
        await WaitForRenderCompletion().ConfigureAwait(true);

        var previouslySelected = tabStrip.Items[0];

        // Act - Select second item
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = previouslySelected.IsSelected.Should().BeFalse("Previous item should be deselected");
        _ = tabStrip.Items[1].IsSelected.Should().BeTrue("New item should be selected");
    });

    [TestMethod]
    public Task SelectItem_RaisesSelectionChanged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        TabSelectionChangedEventArgs? eventArgs = null;
        tabStrip.SelectionChanged += (s, e) => eventArgs = e;

        // Act
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = eventArgs.Should().NotBeNull("SelectionChanged event should be raised");
        _ = eventArgs!.NewItem.Should().Be(tabStrip.Items[1]);

        // Note: Due to "last added item is selected" rule, after construction the last item (Tab 3) is selected.
        _ = eventArgs.OldItem.Should().Be(tabStrip.Items[2], "Previous selection should be the last item (last added)");
        _ = eventArgs.NewIndex.Should().Be(1);
        _ = eventArgs.OldIndex.Should().Be(2);
    });

    [TestMethod]
    public Task SelectPinnedItem_Works_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 2).ConfigureAwait(true);
        var pinnedItem = tabStrip.Items[0];

        // Act
        tabStrip.SelectedItem = pinnedItem;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(pinnedItem, "Pinned items should be selectable");
        _ = tabStrip.SelectedIndex.Should().Be(0);
    });

    [TestMethod]
    public Task SelectItem_ByIndex_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        // Act
        tabStrip.SelectedIndex = 2;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(tabStrip.Items[2], "Setting SelectedIndex should update SelectedItem");
        _ = tabStrip.SelectedIndex.Should().Be(2);
    });

    [TestMethod]
    public Task ClearSelection_OnNonEmptyTabStrip_EnforcesInvariant_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var previouslySelected = tabStrip.SelectedItem; // Will be Items[2] due to "last added" rule
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - Try to clear selection on non-empty TabStrip
        tabStrip.SelectedItem = null;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Invariant enforced: non-empty TabStrip must have a selection
        // When null is set, it restores previous selection (Items[1]) if still valid, else defaults to first
        _ = tabStrip.SelectedItem.Should().Be(tabStrip.Items[1], "Non-empty TabStrip must have a selected item (restores previous selection)");
        _ = tabStrip.SelectedIndex.Should().Be(1, "SelectedIndex should reflect the restored selection");
    });

    [TestMethod]
    public Task ClearSelection_RestoresCurrentSelection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        tabStrip.SelectedItem = tabStrip.Items[1];
        var itemToRemove = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Remove the selected item (this will auto-select right neighbor: originally Tab 3, now at index 1)
        _ = tabStrip.Items.Remove(itemToRemove);
        await WaitForRenderCompletion().ConfigureAwait(true);

        var currentSelection = tabStrip.SelectedItem; // Tab 3 (now at index 1)

        // Act - Try to clear selection
        tabStrip.SelectedItem = null;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Invariant enforced: restores the current selection (Tab 3)
        _ = tabStrip.SelectedItem.Should().Be(currentSelection, "Should restore current selection when attempting to clear");
        _ = tabStrip.SelectedIndex.Should().Be(1, "SelectedIndex should remain at 1");
    });

    [TestMethod]
    public Task ItemIsSelected_SyncsWithSelectedItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        // Act
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items[1].IsSelected.Should().BeTrue("Item's IsSelected should sync with SelectedItem");
        _ = tabStrip.Items[0].IsSelected.Should().BeFalse();
        _ = tabStrip.Items[2].IsSelected.Should().BeFalse();
    });

    [TestMethod]
    public Task RemovingSelectedItem_SelectsRightNeighbor_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var selectedItem = tabStrip.Items[1];
        tabStrip.SelectedItem = selectedItem;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        _ = tabStrip.Items.Remove(selectedItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - the control selects the right neighbor (or clamps to last item)
        _ = tabStrip.SelectedItem.Should().Be(tabStrip.Items[1], "Right neighbor should be selected when selected item is removed");
        _ = tabStrip.SelectedIndex.Should().Be(1);
    });

    [TestMethod]
    public Task PinningSelectedItem_MaintainsSelection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 0).ConfigureAwait(true);
        var item = tabStrip.Items[1];
        tabStrip.SelectedItem = item;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - Pin the selected item
        item.IsPinned = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(item, "Selection should be maintained when item is pinned");
        _ = item.IsSelected.Should().BeTrue();
    });

    [TestMethod]
    public Task UnpinningSelectedItem_MaintainsSelection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 3).ConfigureAwait(true);
        var item = tabStrip.Items[1];
        tabStrip.SelectedItem = item;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - Unpin the selected item
        item.IsPinned = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(item, "Selection should be maintained when item is unpinned");
        _ = item.IsSelected.Should().BeTrue();
    });

    [TestMethod]
    public Task SelectionChanged_HasCorrectOldItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var firstItem = tabStrip.Items[0];
        tabStrip.SelectedItem = firstItem;
        await WaitForRenderCompletion().ConfigureAwait(true);

        TabSelectionChangedEventArgs? eventArgs = null;
        tabStrip.SelectionChanged += (s, e) => eventArgs = e;

        // Act - Select different item
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.OldItem.Should().Be(firstItem, "OldItem should be the previously selected item");
        _ = eventArgs.OldIndex.Should().Be(0);
    });

    [TestMethod]
    public Task SelectionChanged_HasCorrectNewItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        // Note: Items[2] is already selected (last added), so select a different item first
        tabStrip.SelectedItem = tabStrip.Items[0];
        await WaitForRenderCompletion().ConfigureAwait(true);

        TabSelectionChangedEventArgs? eventArgs = null;
        tabStrip.SelectionChanged += (s, e) => eventArgs = e;

        // Act - Select Items[1]
        var newItem = tabStrip.Items[1];
        tabStrip.SelectedItem = newItem;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.NewItem.Should().Be(newItem, "NewItem should be the newly selected item");
        _ = eventArgs.NewIndex.Should().Be(1);
    });

    [TestMethod]
    public Task SelectionChanged_FiresWhenAttemptingToClearOnNonEmptyTabStrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        TabSelectionChangedEventArgs? eventArgs = null;
        tabStrip.SelectionChanged += (s, e) => eventArgs = e;

        // Act - Try to clear selection (invariant will restore previous selection)
        tabStrip.SelectedItem = null;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Invariant enforces selection by restoring previous item (Items[1])
        _ = eventArgs.Should().NotBeNull("Event should fire when selection changes");
        _ = eventArgs!.NewItem.Should().Be(tabStrip.Items[1], "NewItem should be the restored previous selection");
        _ = eventArgs.NewIndex.Should().Be(1, "NewIndex should be 1");
        _ = eventArgs.OldItem.Should().BeNull("OldItem should be null (attempted to clear)");
    });

    [TestMethod]
    public Task SelectOutOfRangeIndex_ClampsToLastItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - Set out-of-range index
        tabStrip.SelectedIndex = 99;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Index clamped to valid range [0, Count-1]
        _ = tabStrip.SelectedItem.Should().Be(tabStrip.Items[^1], "Out-of-range SelectedIndex should clamp to the last item");
        _ = tabStrip.SelectedIndex.Should().Be(tabStrip.Items.Count - 1, "SelectedIndex should be clamped to last valid index");
    });

    [TestMethod]
    public Task SelectNegativeIndex_ClampsToFirstItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - Set negative index (including -1)
        tabStrip.SelectedIndex = -1;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Invariant enforced: index clamped to valid range [0, Count-1]
        _ = tabStrip.SelectedItem.Should().Be(tabStrip.Items[0], "Negative SelectedIndex should clamp to the first item");
        _ = tabStrip.SelectedIndex.Should().Be(0, "SelectedIndex should be clamped to 0");
    });

    [TestMethod]
    public Task SelectItemNotInCollection_ClampsToFirstItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var externalItem = new TabItem { Header = "External" };

        // Act - Try to select an item not in the Items collection
        tabStrip.SelectedItem = externalItem;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Invariant enforced: invalid item defaults to first item
        _ = tabStrip.SelectedItem.Should().Be(tabStrip.Items[0], "Selecting item not in collection should clamp to first item");
        _ = tabStrip.SelectedIndex.Should().Be(0);
        _ = externalItem.IsSelected.Should().BeFalse("External item should not be marked as selected");
    });

    [TestMethod]
    public Task InitialSelection_OnNonEmptyTabStrip_SelectsLastAddedItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        // Assert - Invariant: non-empty TabStrip always has a selection
        // Rule: last added item is selected (applies during construction too)
        _ = tabStrip.SelectedItem.Should().Be(tabStrip.Items[2], "Last added item should be selected after construction");
        _ = tabStrip.SelectedIndex.Should().Be(2);
        _ = tabStrip.Items[2].IsSelected.Should().BeTrue("Last item should be marked as selected");
    });

    [TestMethod]
    public Task AddingMultipleItems_SelectsLastAddedItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(2).ConfigureAwait(true);

        // Act - Add multiple items
        var item1 = new TabItem { Header = "New Item 1" };
        var item2 = new TabItem { Header = "New Item 2" };
        var item3 = new TabItem { Header = "New Item 3" };

        tabStrip.Items.Add(item1);
        await WaitForRenderCompletion().ConfigureAwait(true);

        tabStrip.Items.Add(item2);
        await WaitForRenderCompletion().ConfigureAwait(true);

        tabStrip.Items.Add(item3);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Rule: last added item is selected
        _ = tabStrip.SelectedItem.Should().Be(item3, "Last added item should be selected");
        _ = item3.IsSelected.Should().BeTrue();
    });

    [TestMethod]
    public Task RemovingLastItem_WhenSelected_SelectsPreviousItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var lastItem = tabStrip.Items[^1];
        var previousItem = tabStrip.Items[^2];
        tabStrip.SelectedItem = lastItem;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - Remove the last (selected) item
        _ = tabStrip.Items.Remove(lastItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Previous item (now last) should be selected
        _ = tabStrip.SelectedItem.Should().Be(previousItem, "Previous item should be selected when last item is removed");
        _ = tabStrip.SelectedIndex.Should().Be(tabStrip.Items.Count - 1, "SelectedIndex should be the last valid index");
        _ = previousItem.IsSelected.Should().BeTrue();
    });

    [TestMethod]
    public Task RemovingFirstItem_WhenSelected_SelectsRightNeighbor_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var firstItem = tabStrip.Items[0];
        var rightNeighbor = tabStrip.Items[1];
        tabStrip.SelectedItem = firstItem;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - Remove the first (selected) item
        _ = tabStrip.Items.Remove(firstItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Right neighbor (now at index 0) should be selected
        _ = tabStrip.SelectedItem.Should().Be(rightNeighbor, "Right neighbor should be selected when first item is removed");
        _ = tabStrip.SelectedIndex.Should().Be(0, "SelectedIndex should be 0");
        _ = rightNeighbor.IsSelected.Should().BeTrue();
    });

    [TestMethod]
    public Task MultipleSelectionChanges_AllEventsRaised_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(4).ConfigureAwait(true);

        var eventCount = 0;
        tabStrip.SelectionChanged += (s, e) => eventCount++;

        // Act - Perform multiple selection changes
        tabStrip.SelectedItem = tabStrip.Items[0];
        await WaitForRenderCompletion().ConfigureAwait(true);

        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        tabStrip.SelectedItem = tabStrip.Items[2];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = eventCount.Should().Be(3, "Event should fire for each selection change");
    });
}
