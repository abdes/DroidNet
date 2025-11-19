// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Aura.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
[TestCategory("TabStrip.Collections")]
public class TabStripCollectionTests : TabStripTestsBase
{
    public TestContext TestContext { get; set; } = default!;

    [TestMethod]
    public Task PinnedItem_AppearsInPinnedRepeater_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);

        // Act
        var pinnedItem = CreateTabItem("Pinned Tab", isPinned: true);
        tabStrip.Items.Add(pinnedItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.PinnedItemsView.Should().Contain(pinnedItem, "Pinned item should be in pinned view");
        _ = tabStrip.RegularItemsView.Should().NotContain(pinnedItem, "Pinned item should not be in regular view");
        _ = tabStrip.PinnedItemsView.Count.Should().Be(1);
    });

    [TestMethod]
    public Task UnpinnedItem_AppearsInRegularRepeater_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);

        // Act
        var regularItem = CreateTabItem("Regular Tab", isPinned: false);
        tabStrip.Items.Add(regularItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.RegularItemsView.Should().Contain(regularItem, "Regular item should be in regular view");
        _ = tabStrip.PinnedItemsView.Should().NotContain(regularItem, "Regular item should not be in pinned view");
        _ = tabStrip.RegularItemsView.Count.Should().Be(1);
    });

    [TestMethod]
    public Task TogglePin_MovesItemBetweenRepeaters_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(1, pinnedCount: 0).ConfigureAwait(true);
        var item = tabStrip.Items[0];

        // Verify initial state - item is unpinned
        _ = item.IsPinned.Should().BeFalse();
        _ = tabStrip.RegularItemsView.Should().Contain(item);
        _ = tabStrip.PinnedItemsView.Should().NotContain(item);

        // Act - Pin the item
        item.IsPinned = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Item moved to pinned view
        _ = tabStrip.PinnedItemsView.Should().Contain(item, "Item should move to pinned view");
        _ = tabStrip.RegularItemsView.Should().NotContain(item, "Item should leave regular view");

        // Act - Unpin the item
        item.IsPinned = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Item moved back to regular view
        _ = tabStrip.RegularItemsView.Should().Contain(item, "Item should move back to regular view");
        _ = tabStrip.PinnedItemsView.Should().NotContain(item, "Item should leave pinned view");
    });

    [TestMethod]
    public Task MultiplePinnedItems_MaintainOrder_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);

        // Act - Add multiple pinned items
        var pinned1 = CreateTabItem("Pinned 1", isPinned: true);
        var pinned2 = CreateTabItem("Pinned 2", isPinned: true);
        var pinned3 = CreateTabItem("Pinned 3", isPinned: true);

        tabStrip.Items.Add(pinned1);
        tabStrip.Items.Add(pinned2);
        tabStrip.Items.Add(pinned3);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Order is maintained
        _ = tabStrip.PinnedItemsView.Should().NotBeNull();
        _ = tabStrip.PinnedItemsView.Should().Equal(pinned1, pinned2, pinned3);
    });

    [TestMethod]
    public Task MixedItems_SegregateCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(0).ConfigureAwait(true);

        // Act - Add mixed pinned and regular items
        var pinned1 = CreateTabItem("Pinned 1", isPinned: true);
        var regular1 = CreateTabItem("Regular 1", isPinned: false);
        var pinned2 = CreateTabItem("Pinned 2", isPinned: true);
        var regular2 = CreateTabItem("Regular 2", isPinned: false);

        tabStrip.Items.Add(pinned1);
        tabStrip.Items.Add(regular1);
        tabStrip.Items.Add(pinned2);
        tabStrip.Items.Add(regular2);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Items are correctly segregated
        _ = tabStrip.PinnedItemsView.Should().Equal(pinned1, pinned2);
        _ = tabStrip.RegularItemsView.Should().Equal(regular1, regular2);
    });

    [TestMethod]
    public Task PinningLastItem_UpdatesBothViews_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 0).ConfigureAwait(true);
        var lastItem = tabStrip.Items[2];

        // Verify initial state
        _ = tabStrip.RegularItemsView.Should().NotBeNull().And.HaveCount(3);
        _ = tabStrip.PinnedItemsView.Should().NotBeNull().And.BeEmpty();

        // Act
        lastItem.IsPinned = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.PinnedItemsView.Count.Should().Be(1);
        _ = tabStrip.RegularItemsView.Count.Should().Be(2);
        _ = tabStrip.PinnedItemsView.Should().Contain(lastItem);
    });

    [TestMethod]
    public Task AllPinned_RegularViewEmpty_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var tabStrip = await this.CreateAndLoadTabStripAsync(5, pinnedCount: 5).ConfigureAwait(true);

        // Assert
        _ = tabStrip.PinnedItemsView.Should().NotBeNull().And.HaveCount(5, "All items should be pinned");
        _ = tabStrip.RegularItemsView.Should().NotBeNull().And.BeEmpty("No items should be in regular view");
    });

    [TestMethod]
    public Task AllRegular_PinnedViewEmpty_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var tabStrip = await this.CreateAndLoadTabStripAsync(5, pinnedCount: 0).ConfigureAwait(true);

        // Assert
        _ = tabStrip.RegularItemsView.Should().NotBeNull().And.HaveCount(5, "All items should be regular");
        _ = tabStrip.PinnedItemsView.Should().NotBeNull().And.BeEmpty("No items should be in pinned view");
    });

    [TestMethod]
    public Task RemovePinnedItem_UpdatesPinnedView_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 2).ConfigureAwait(true);
        _ = tabStrip.PinnedItemsView.Should().NotBeNull();
        var pinnedItem = tabStrip.PinnedItemsView[0];

        // Act
        _ = tabStrip.Items.Remove(pinnedItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.PinnedItemsView.Count.Should().Be(1, "Pinned view should decrease");
        _ = tabStrip.PinnedItemsView.Should().NotContain(pinnedItem);
        _ = tabStrip.Items.Should().HaveCount(2);
    });

    [TestMethod]
    public Task RemoveRegularItem_UpdatesRegularView_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3, pinnedCount: 1).ConfigureAwait(true);
        _ = tabStrip.RegularItemsView.Should().NotBeNull().And.HaveCount(2);
        var regularItem = tabStrip.RegularItemsView[0];

        // Act
        _ = tabStrip.Items.Remove(regularItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.RegularItemsView.Count.Should().Be(1, "Regular view should decrease");
        _ = tabStrip.RegularItemsView.Should().NotContain(regularItem);
        _ = tabStrip.PinnedItemsView.Should().NotBeNull().And.HaveCount(1, "Pinned view should be unchanged");
    });

    [TestMethod]
    public Task RapidPinUnpin_HandledCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(1).ConfigureAwait(true);
        var item = tabStrip.Items[0];

        // Act - Rapidly toggle pin state
        for (var i = 0; i < 5; i++)
        {
            item.IsPinned = !item.IsPinned;
            await WaitForRenderCompletion().ConfigureAwait(true);
        }

        // Assert - Final state should be unpinned (started false, toggled 5 times)
        _ = item.IsPinned.Should().BeTrue();
        _ = tabStrip.PinnedItemsView.Should().Contain(item);
        _ = tabStrip.RegularItemsView.Should().NotContain(item);
        _ = tabStrip.PinnedItemsView.Count.Should().Be(1);
        _ = tabStrip.RegularItemsView.Count.Should().Be(0);
    });

    [TestMethod]
    public Task InsertPinnedItem_InMiddle_AppearsInPinnedView_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(4, pinnedCount: 2).ConfigureAwait(true);

        // Act - Insert a pinned item in the middle of Items collection
        var newPinnedItem = CreateTabItem("Inserted Pinned", isPinned: true);
        tabStrip.Items.Insert(2, newPinnedItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.PinnedItemsView.Should().Contain(newPinnedItem);
        _ = tabStrip.PinnedItemsView.Count.Should().Be(3);
        _ = tabStrip.Items.Should().HaveCount(5);
    });
}
