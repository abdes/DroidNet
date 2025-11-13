// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Aura.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public class TabStripEventsTests : TabStripTestsBase
{
    public TestContext TestContext { get; set; } = default!;

    [TestMethod]
    public Task SelectionChanged_HasCorrectOldItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        tabStrip.SelectedItem = tabStrip.Items[0];
        await WaitForRenderCompletion().ConfigureAwait(true);

        TabSelectionChangedEventArgs? eventArgs = null;
        tabStrip.SelectionChanged += (s, e) => eventArgs = e;

        // Act
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.OldItem.Should().Be(tabStrip.Items[0]);
    });

    [TestMethod]
    public Task SelectionChanged_HasCorrectNewItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        TabSelectionChangedEventArgs? eventArgs = null;
        tabStrip.SelectionChanged += (s, e) => eventArgs = e;

        // Act
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.NewItem.Should().Be(tabStrip.Items[1]);
    });

    [TestMethod]
    public Task SelectionChanged_HasCorrectIndices_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        tabStrip.SelectedIndex = 0;
        await WaitForRenderCompletion().ConfigureAwait(true);

        TabSelectionChangedEventArgs? eventArgs = null;
        tabStrip.SelectionChanged += (s, e) => eventArgs = e;

        // Act
        tabStrip.SelectedIndex = 2;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.OldIndex.Should().Be(0);
        _ = eventArgs!.NewIndex.Should().Be(2);
    });

    [TestMethod]
    public Task SelectionChanged_FiresOnClear_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        var previouslySelected = tabStrip.Items[1];
        tabStrip.SelectedItem = previouslySelected;
        await WaitForRenderCompletion().ConfigureAwait(true);

        TabSelectionChangedEventArgs? eventArgs = null;
        tabStrip.SelectionChanged += (s, e) => eventArgs = e;

        // Act - Remove all items to allow null selection (TabStrip invariant: non-empty collection must have selection)
        tabStrip.Items.Clear();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Should have fired SelectionChanged with old item and null new item
        _ = eventArgs.Should().NotBeNull("SelectionChanged should fire when items are cleared");
        _ = eventArgs!.OldItem.Should().Be(previouslySelected, "the previously selected item should be in OldItem");
        _ = eventArgs!.NewItem.Should().BeNull("after clearing, selection should be null");
        _ = tabStrip.SelectedItem.Should().BeNull("TabStrip.SelectedItem should be null after clearing");
    });

    [TestMethod]
    public Task SelectionChanged_PassesSenderAsTabStrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(2).ConfigureAwait(true);

        object? sender = null;
        tabStrip.SelectionChanged += (s, e) => sender = s;

        // Act
        tabStrip.SelectedItem = tabStrip.Items[0];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = sender.Should().BeSameAs(tabStrip);
    });

    [TestMethod]
    public Task TabActivated_PassesCorrectItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        var targetItem = tabStrip.Items[1];
        TabActivatedEventArgs? eventArgs = null;
        tabStrip.TabActivated += (s, e) => eventArgs = e;

        // Act - Simulate tap using TestableTabStrip helper
        var container = tabStrip.GetContainerForIndex(1);
        _ = container.Should().NotBeNull().And.BeOfType<TabStripItem>();
        tabStrip.SimulateTap((TabStripItem)container!);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.Item.Should().Be(targetItem);
    });

    [TestMethod]
    public Task TabActivated_PassesCorrectIndex_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        TabActivatedEventArgs? eventArgs = null;
        tabStrip.TabActivated += (s, e) => eventArgs = e;

        // Act
        var container = tabStrip.GetContainerForIndex(2);
        _ = container.Should().NotBeNull().And.BeOfType<TabStripItem>();
        tabStrip.SimulateTap((TabStripItem)container!);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.Index.Should().Be(2);
    });

    [TestMethod]
    public Task TabActivated_SelectsTheTab_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        // Act
        var container = tabStrip.GetContainerForIndex(1);
        _ = container.Should().NotBeNull().And.BeOfType<TabStripItem>();
        tabStrip.SimulateTap((TabStripItem)container!);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.SelectedItem.Should().Be(tabStrip.Items[1]);
        _ = tabStrip.SelectedIndex.Should().Be(1);
    });

    [TestMethod]
    public Task TabCloseRequested_PassesCorrectItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        var targetItem = tabStrip.Items[1];
        targetItem.IsClosable = true;

        TabCloseRequestedEventArgs? eventArgs = null;
        tabStrip.TabCloseRequested += (s, e) => eventArgs = e;

        // Act - Simulate close button click using the helper
        tabStrip.HandleTabCloseRequest(targetItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.Item.Should().Be(targetItem);
    });

    [TestMethod]
    public Task TabCloseRequested_DoesNotRemoveItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        var targetItem = tabStrip.Items[1];
        targetItem.IsClosable = true;

        tabStrip.TabCloseRequested += (s, e) => { /* Do nothing - don't remove */ };

        // Act - Simulate close button click using the helper
        tabStrip.HandleTabCloseRequest(targetItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().HaveCount(3);
        _ = tabStrip.Items.Should().Contain(targetItem);
    });

    [TestMethod]
    public Task TabCloseRequested_AllowsHandlerToRemoveItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        var targetItem = tabStrip.Items[1];
        targetItem.IsClosable = true;

        tabStrip.TabCloseRequested += (s, e) =>
        {
            if (e.Item is TabItem item)
            {
                _ = tabStrip.Items.Remove(item);
            }
        };

        // Act - Simulate close button click using the helper
        tabStrip.HandleTabCloseRequest(targetItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().HaveCount(2);
        _ = tabStrip.Items.Should().NotContain(targetItem);
    });

    [TestMethod]
    public Task TabCloseRequested_PassesSenderAsTabStrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(2).ConfigureAwait(true);

        var targetItem = tabStrip.Items[0];
        targetItem.IsClosable = true;

        object? sender = null;
        tabStrip.TabCloseRequested += (s, e) => sender = s;

        // Act - Simulate close button click using the helper
        tabStrip.HandleTabCloseRequest(targetItem);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = sender.Should().BeSameAs(tabStrip);
    });

    [TestMethod]
    public Task MultipleEventHandlers_AllFire_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        var handler1Fired = false;
        var handler2Fired = false;
        var handler3Fired = false;

        tabStrip.SelectionChanged += (s, e) => handler1Fired = true;
        tabStrip.SelectionChanged += (s, e) => handler2Fired = true;
        tabStrip.SelectionChanged += (s, e) => handler3Fired = true;

        // Act
        tabStrip.SelectedItem = tabStrip.Items[0];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = handler1Fired.Should().BeTrue();
        _ = handler2Fired.Should().BeTrue();
        _ = handler3Fired.Should().BeTrue();
    });

    [TestMethod]
    public Task UnsubscribedHandler_DoesNotFire_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(2).ConfigureAwait(true);

        var handlerFired = false;
        void Handler(object? s, TabSelectionChangedEventArgs e) => handlerFired = true;

        tabStrip.SelectionChanged += Handler;
        tabStrip.SelectionChanged -= Handler; // Unsubscribe immediately

        // Act
        tabStrip.SelectedItem = tabStrip.Items[0];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = handlerFired.Should().BeFalse();
    });

    [TestMethod]
    public Task EventHandler_CanModifyCollection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);

        var handlerCallCount = 0;
        var itemsAddedCount = 0;
        tabStrip.SelectionChanged += (s, e) =>
        {
            handlerCallCount++;

            // Modify collection during event handler - only add once per handler call
            var newItem = CreateTabItem("New Tab");
            tabStrip.Items.Add(newItem);
            itemsAddedCount++;
        };

        // Act - Trigger SelectionChanged by changing selection
        // Initial state: record initial count and then change selection twice
        var initialCount = tabStrip.Items.Count;

        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        tabStrip.SelectedItem = tabStrip.Items[0];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        // The handler should have been called at least twice. The collection size
        // should equal the initial count plus the number of handler calls (each call adds one).
        _ = handlerCallCount.Should().BeGreaterThanOrEqualTo(2, "handler should have been called at least twice (one per selection change)");
        _ = tabStrip.Items.Should().HaveCount(initialCount + handlerCallCount, "items should have increased by the number of handler invocations");
        _ = itemsAddedCount.Should().Be(handlerCallCount, "itemsAddedCount should match handlerCallCount");
    });

    [TestMethod]
    public Task SelectionChanged_NotFiredForSameSelection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = await this.CreateAndLoadTabStripAsync(3).ConfigureAwait(true);
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        var eventCount = 0;
        tabStrip.SelectionChanged += (s, e) => eventCount++;

        // Act - Set to same item
        tabStrip.SelectedItem = tabStrip.Items[1];
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = eventCount.Should().Be(0, "event should not fire when selection doesn't change");
    });
}
