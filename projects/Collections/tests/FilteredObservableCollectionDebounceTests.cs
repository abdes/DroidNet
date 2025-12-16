// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Collections.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Filtered Observable Collection")]
[TestCategory("Debounce")]
public class FilteredObservableCollectionDebounceTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task Debounce_BatchesMultipleAdds_ContiguousInView_UsesSingleAddEvent()
    {
        // Arrange
        var i0 = new ObservableItem(1);
        var i1 = new ObservableItem(1);
        var i2 = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { i0, i1, i2 };

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(30) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0, opts);

        var addEvents = new List<NotifyCollectionChangedEventArgs>();
        var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        view.CollectionChanged += (_, e) =>
        {
            if (e.Action == NotifyCollectionChangedAction.Add)
            {
                addEvents.Add(e);
                _ = tcs.TrySetResult(null);
            }
        };

        // Act - two changes inside the debounce window; source indices 0 and 2 are not adjacent,
        // but in the filtered view they become adjacent because item 1 remains excluded.
        i0.Value = 2;
        i2.Value = 2;

        _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken).ConfigureAwait(false);
        await Task.Delay(TimeSpan.FromMilliseconds(50), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = view.Should().HaveCount(2);
        _ = view[0].Should().BeSameAs(i0);
        _ = view[1].Should().BeSameAs(i2);

        _ = addEvents.Should().ContainSingle();
        _ = addEvents[0].NewStartingIndex.Should().Be(0);
        _ = addEvents[0].NewItems.Should().NotBeNull();
        _ = addEvents[0].NewItems!.Count.Should().Be(2);
        _ = addEvents[0].NewItems![0].Should().BeSameAs(i0);
        _ = addEvents[0].NewItems![1].Should().BeSameAs(i2);
    }

    [TestMethod]
    public async Task Debounce_BatchesMultipleRemoves_ContiguousInView_UsesSingleRemoveEvent()
    {
        // Arrange
        var i0 = new ObservableItem(2);
        var i1 = new ObservableItem(1);
        var i2 = new ObservableItem(2);
        var source = new ObservableCollection<ObservableItem> { i0, i1, i2 };

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(30) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0, opts);

        _ = view.Should().HaveCount(2);
        _ = view[0].Should().BeSameAs(i0);
        _ = view[1].Should().BeSameAs(i2);

        var removeEvents = new List<NotifyCollectionChangedEventArgs>();
        var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        view.CollectionChanged += (_, e) =>
        {
            if (e.Action == NotifyCollectionChangedAction.Remove)
            {
                removeEvents.Add(e);
                _ = tcs.TrySetResult(null);
            }
        };

        // Act - two changes inside the debounce window; both removals are contiguous in the filtered view.
        i0.Value = 1;
        i2.Value = 1;

        _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken).ConfigureAwait(false);
        await Task.Delay(TimeSpan.FromMilliseconds(50), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = view.Should().BeEmpty();

        _ = removeEvents.Should().ContainSingle();
        _ = removeEvents[0].OldStartingIndex.Should().Be(0);
        _ = removeEvents[0].OldItems.Should().NotBeNull();
        _ = removeEvents[0].OldItems!.Count.Should().Be(2);
        _ = removeEvents[0].OldItems![0].Should().BeSameAs(i0);
        _ = removeEvents[0].OldItems![1].Should().BeSameAs(i2);
    }

    [TestMethod]
    public async Task Debounce_MixedAddsAndRemoves_UsesOneRemoveAndOneBatchedAdd()
    {
        // Arrange
        var i0 = new ObservableItem(2);
        var i1 = new ObservableItem(2);
        var i2 = new ObservableItem(1);
        var i3 = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { i0, i1, i2, i3 };

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(30) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0, opts);

        _ = view.Should().HaveCount(2);
        _ = view[0].Should().BeSameAs(i0);
        _ = view[1].Should().BeSameAs(i1);

        var events = new List<NotifyCollectionChangedEventArgs>();
        var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        view.CollectionChanged += (_, e) => events.Add(e);
        view.CollectionChanged += (_, _) => _ = tcs.TrySetResult(null);

        // Act - within one debounce window: remove one item and add two items that become contiguous in view.
        i0.Value = 1;
        i2.Value = 2;
        i3.Value = 2;

        _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken).ConfigureAwait(false);
        await Task.Delay(TimeSpan.FromMilliseconds(50), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = events.Should().OnlyContain(e => e.Action != NotifyCollectionChangedAction.Reset);
        _ = events.Should().HaveCount(2);

        _ = events[0].Action.Should().Be(NotifyCollectionChangedAction.Remove);
        _ = events[0].OldStartingIndex.Should().Be(0);
        _ = events[0].OldItems.Should().NotBeNull();
        _ = events[0].OldItems!.Count.Should().Be(1);
        _ = events[0].OldItems![0].Should().BeSameAs(i0);

        _ = events[1].Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = events[1].NewStartingIndex.Should().Be(1);
        _ = events[1].NewItems.Should().NotBeNull();
        _ = events[1].NewItems!.Count.Should().Be(2);
        _ = events[1].NewItems![0].Should().BeSameAs(i2);
        _ = events[1].NewItems![1].Should().BeSameAs(i3);

        _ = view.Should().HaveCount(3);
        _ = view[0].Should().BeSameAs(i1);
        _ = view[1].Should().BeSameAs(i2);
        _ = view[2].Should().BeSameAs(i3);
    }

    [TestMethod]
    public async Task Debounce_MultipleAdds_NonContiguousInView_UsesMultipleAddEvents()
    {
        // Arrange
        var i0 = new ObservableItem(1);
        var i1 = new ObservableItem(2);
        var i2 = new ObservableItem(1);
        var i3 = new ObservableItem(2);
        var source = new ObservableCollection<ObservableItem> { i0, i1, i2, i3 };

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(200) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0, opts);

        _ = view.Should().HaveCount(2);
        _ = view[0].Should().BeSameAs(i1);
        _ = view[1].Should().BeSameAs(i3);

        var addEvents = new List<NotifyCollectionChangedEventArgs>();
        var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        view.CollectionChanged += (_, e) =>
        {
            if (e.Action == NotifyCollectionChangedAction.Add)
            {
                addEvents.Add(e);
                if (addEvents.Count >= 2)
                {
                    _ = tcs.TrySetResult(null);
                }
            }
        };

        // Act - both become included, but are separated by an already-included item in the final view.
        i0.Value = 2;
        i2.Value = 2;

        _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken).ConfigureAwait(false);
        await Task.Delay(TimeSpan.FromMilliseconds(50), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = view.Should().HaveCount(4);
        _ = view[0].Should().BeSameAs(i0);
        _ = view[1].Should().BeSameAs(i1);
        _ = view[2].Should().BeSameAs(i2);
        _ = view[3].Should().BeSameAs(i3);

        _ = addEvents.Should().HaveCount(2);

        _ = addEvents[0].NewStartingIndex.Should().Be(0);
        _ = addEvents[0].NewItems.Should().NotBeNull();
        _ = addEvents[0].NewItems!.Count.Should().Be(1);
        _ = addEvents[0].NewItems![0].Should().BeSameAs(i0);

        _ = addEvents[1].NewStartingIndex.Should().Be(2);
        _ = addEvents[1].NewItems.Should().NotBeNull();
        _ = addEvents[1].NewItems!.Count.Should().Be(1);
        _ = addEvents[1].NewItems![0].Should().BeSameAs(i2);
    }

    [TestMethod]
    public async Task Debounce_MultipleRemoves_NonContiguousInView_UsesMultipleRemoveEvents()
    {
        // Arrange
        var i0 = new ObservableItem(2);
        var i1 = new ObservableItem(2);
        var i2 = new ObservableItem(2);
        var i3 = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { i0, i1, i2, i3 };

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(200) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0, opts);

        _ = view.Should().HaveCount(3);
        _ = view[0].Should().BeSameAs(i0);
        _ = view[1].Should().BeSameAs(i1);
        _ = view[2].Should().BeSameAs(i2);

        var removeEvents = new List<NotifyCollectionChangedEventArgs>();
        var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        view.CollectionChanged += (_, e) =>
        {
            if (e.Action == NotifyCollectionChangedAction.Remove)
            {
                removeEvents.Add(e);
                if (removeEvents.Count >= 2)
                {
                    _ = tcs.TrySetResult(null);
                }
            }
        };

        // Act - removals are not contiguous because i1 remains included.
        i0.Value = 1;
        i2.Value = 1;

        _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken).ConfigureAwait(false);
        await Task.Delay(TimeSpan.FromMilliseconds(50), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = view.Should().ContainSingle().Which.Should().BeSameAs(i1);

        _ = removeEvents.Should().HaveCount(2);

        // Removals are applied in descending index order.
        _ = removeEvents[0].OldStartingIndex.Should().Be(2);
        _ = removeEvents[0].OldItems.Should().NotBeNull();
        _ = removeEvents[0].OldItems!.Count.Should().Be(1);
        _ = removeEvents[0].OldItems![0].Should().BeSameAs(i2);

        _ = removeEvents[1].OldStartingIndex.Should().Be(0);
        _ = removeEvents[1].OldItems.Should().NotBeNull();
        _ = removeEvents[1].OldItems!.Count.Should().Be(1);
        _ = removeEvents[1].OldItems![0].Should().BeSameAs(i0);
    }

    [TestMethod]
    public async Task Debounce_MultipleAdds_ChangedOutOfSourceOrder_ViewAndEventRemainSourceOrdered()
    {
        // Arrange
        var i0 = new ObservableItem(1);
        var i1 = new ObservableItem(1);
        var i2 = new ObservableItem(1);
        var i3 = new ObservableItem(1);
        var i4 = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { i0, i1, i2, i3, i4 };

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(30) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0, opts);

        _ = view.Should().BeEmpty();

        var addEvents = new List<NotifyCollectionChangedEventArgs>();
        var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        view.CollectionChanged += (_, e) =>
        {
            if (e.Action == NotifyCollectionChangedAction.Add)
            {
                addEvents.Add(e);
                _ = tcs.TrySetResult(null);
            }
        };

        // Act - change later item first; dirtyItems is a HashSet so processing order is not guaranteed.
        i4.Value = 2;
        i1.Value = 2;

        _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken).ConfigureAwait(false);
        await Task.Delay(TimeSpan.FromMilliseconds(50), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert - view and event items must be in source order.
        _ = view.Should().HaveCount(2);
        _ = view[0].Should().BeSameAs(i1);
        _ = view[1].Should().BeSameAs(i4);

        _ = addEvents.Should().ContainSingle();
        _ = addEvents[0].NewStartingIndex.Should().Be(0);
        _ = addEvents[0].NewItems.Should().NotBeNull();
        _ = addEvents[0].NewItems!.Count.Should().Be(2);
        _ = addEvents[0].NewItems![0].Should().BeSameAs(i1);
        _ = addEvents[0].NewItems![1].Should().BeSameAs(i4);
    }

    [TestMethod]
    public async Task Debounce_MultipleRemoves_ChangedOutOfSourceOrder_ViewAndEventRemainSourceOrdered()
    {
        // Arrange
        var i0 = new ObservableItem(1);
        var i1 = new ObservableItem(2);
        var i2 = new ObservableItem(1);
        var i3 = new ObservableItem(1);
        var i4 = new ObservableItem(2);
        var source = new ObservableCollection<ObservableItem> { i0, i1, i2, i3, i4 };

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(30) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0, opts);

        _ = view.Should().HaveCount(2);
        _ = view[0].Should().BeSameAs(i1);
        _ = view[1].Should().BeSameAs(i4);

        var removeEvents = new List<NotifyCollectionChangedEventArgs>();
        var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        view.CollectionChanged += (_, e) =>
        {
            if (e.Action == NotifyCollectionChangedAction.Remove)
            {
                removeEvents.Add(e);
                _ = tcs.TrySetResult(null);
            }
        };

        // Act - change later item first.
        i4.Value = 1;
        i1.Value = 1;

        _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken).ConfigureAwait(false);
        await Task.Delay(TimeSpan.FromMilliseconds(50), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert - removals must be reported in source/view order.
        _ = view.Should().BeEmpty();

        _ = removeEvents.Should().ContainSingle();
        _ = removeEvents[0].OldStartingIndex.Should().Be(0);
        _ = removeEvents[0].OldItems.Should().NotBeNull();
        _ = removeEvents[0].OldItems!.Count.Should().Be(2);
        _ = removeEvents[0].OldItems![0].Should().BeSameAs(i1);
        _ = removeEvents[0].OldItems![1].Should().BeSameAs(i4);
    }

    [TestMethod]
    public async Task Debounce_MultipleAdds_WithExistingIncludedAnchors_EventsFollowSourceOrderAndIndices()
    {
        // Arrange
        var i0 = new ObservableItem(1);
        var i1 = new ObservableItem(1);
        var i2 = new ObservableItem(2); // included anchor
        var i3 = new ObservableItem(1);
        var i4 = new ObservableItem(1);
        var i5 = new ObservableItem(2); // included anchor
        var i6 = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { i0, i1, i2, i3, i4, i5, i6 };

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(30) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0, opts);

        _ = view.Should().Equal([i2, i5]);

        var addEvents = new List<NotifyCollectionChangedEventArgs>();
        var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        view.CollectionChanged += (_, e) =>
        {
            if (e.Action == NotifyCollectionChangedAction.Add)
            {
                addEvents.Add(e);
                if (addEvents.Count >= 3)
                {
                    _ = tcs.TrySetResult(null);
                }
            }
        };

        // Act - change out of source order.
        i6.Value = 2;
        i1.Value = 2;
        i4.Value = 2;

        _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken).ConfigureAwait(false);
        await Task.Delay(TimeSpan.FromMilliseconds(50), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert - view follows source order.
        _ = view.Should().Equal([i1, i2, i4, i5, i6]);

        // Add events must also be in source order with stable indices.
        _ = addEvents.Should().HaveCount(3);

        _ = addEvents[0].NewStartingIndex.Should().Be(0);
        _ = addEvents[0].NewItems.Should().NotBeNull();
        _ = addEvents[0].NewItems!.Count.Should().Be(1);
        _ = addEvents[0].NewItems![0].Should().BeSameAs(i1);

        _ = addEvents[1].NewStartingIndex.Should().Be(2);
        _ = addEvents[1].NewItems.Should().NotBeNull();
        _ = addEvents[1].NewItems!.Count.Should().Be(1);
        _ = addEvents[1].NewItems![0].Should().BeSameAs(i4);

        _ = addEvents[2].NewStartingIndex.Should().Be(4);
        _ = addEvents[2].NewItems.Should().NotBeNull();
        _ = addEvents[2].NewItems!.Count.Should().Be(1);
        _ = addEvents[2].NewItems![0].Should().BeSameAs(i6);
    }

    [TestMethod]
    public async Task Debounce_MixedAddsAndRemoves_WithAnchors_EventsAndFinalViewPreserveSourceOrder()
    {
        // Arrange
        var i0 = new ObservableItem(2); // anchor (included)
        var i1 = new ObservableItem(2); // will be removed
        var i2 = new ObservableItem(1); // will be added
        var i3 = new ObservableItem(2); // anchor (included)
        var i4 = new ObservableItem(1); // will be added
        var i5 = new ObservableItem(2); // will be removed
        var i6 = new ObservableItem(2); // anchor (included)
        var i7 = new ObservableItem(1); // will be added
        var i8 = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { i0, i1, i2, i3, i4, i5, i6, i7, i8 };

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(30) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0, opts);

        _ = view.Should().Equal([i0, i1, i3, i5, i6]);

        var events = new List<NotifyCollectionChangedEventArgs>();
        var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        view.CollectionChanged += (_, e) =>
        {
            if (e.Action is NotifyCollectionChangedAction.Add or NotifyCollectionChangedAction.Remove)
            {
                events.Add(e);
                if (events.Count >= 5)
                {
                    _ = tcs.TrySetResult(null);
                }
            }
        };

        // Act - make changes out of source order to stress any nondeterministic iteration.
        i5.Value = 1; // remove
        i7.Value = 2; // add
        i1.Value = 1; // remove
        i2.Value = 2; // add
        i4.Value = 2; // add

        _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken).ConfigureAwait(false);
        await Task.Delay(TimeSpan.FromMilliseconds(50), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert - final view must be strictly source-ordered.
        _ = view.Should().Equal([i0, i2, i3, i4, i6, i7]);

        // Structural event sequence must be deterministic:
        // - removals first, descending by old index
        // - then adds in ascending source order with correct insertion indices
        _ = events.Should().HaveCount(5);

        AssertSingleRemove(events[0], oldStartingIndex: 3, i5);
        AssertSingleRemove(events[1], oldStartingIndex: 1, i1);
        AssertSingleAdd(events[2], newStartingIndex: 1, i2);
        AssertSingleAdd(events[3], newStartingIndex: 3, i4);
        AssertSingleAdd(events[4], newStartingIndex: 5, i7);
    }

    [TestMethod]
    public async Task Debounce_MixedAddsAndRemoves_AddsBecomeContiguous_UsesSingleBatchedAddEvent()
    {
        // Arrange
        var i0 = new ObservableItem(2); // anchor
        var i1 = new ObservableItem(2); // will be removed
        var i2 = new ObservableItem(1); // will be added
        var i3 = new ObservableItem(1); // will be added
        var i4 = new ObservableItem(1); // will be added
        var i5 = new ObservableItem(2); // anchor
        var source = new ObservableCollection<ObservableItem> { i0, i1, i2, i3, i4, i5 };

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(30) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0, opts);

        _ = view.Should().Equal([i0, i1, i5]);

        var events = new List<NotifyCollectionChangedEventArgs>();
        var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        view.CollectionChanged += (_, e) =>
        {
            if (e.Action is NotifyCollectionChangedAction.Add or NotifyCollectionChangedAction.Remove)
            {
                events.Add(e);
                if (events.Count >= 2)
                {
                    _ = tcs.TrySetResult(null);
                }
            }
        };

        // Act - change out of source order.
        i4.Value = 2;
        i2.Value = 2;
        i1.Value = 1;
        i3.Value = 2;

        _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken).ConfigureAwait(false);
        await Task.Delay(TimeSpan.FromMilliseconds(50), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert - final view remains source ordered and added items are contiguous.
        _ = view.Should().Equal([i0, i2, i3, i4, i5]);

        _ = events.Should().HaveCount(2);
        AssertSingleRemove(events[0], oldStartingIndex: 1, i1);
        AssertAddItems(events[1], newStartingIndex: 1, i2, i3, i4);
    }

    [TestMethod]
    public async Task Debounce_CoalescesMultiplePropertyChanges()
    {
        // Arrange
        var item = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { item };
        var builder = new RecordingPassThroughBuilder();

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(30) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, _ => true, builder, opts);

        var collectionChangedCount = 0;
        view.CollectionChanged += (_, _) => collectionChangedCount++;

        // Act - two changes inside the debounce window
        item.Value = 2;
        await Task.Delay(TimeSpan.FromMilliseconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        item.Value = 3;

        // wait beyond debounce window for scheduled rebuild to fire
        await Task.Delay(TimeSpan.FromMilliseconds(80), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert - property-only changes should not raise CollectionChanged; only one rebuild after coalescing
        _ = collectionChangedCount.Should().Be(0);
        _ = builder.BuildCount.Should().Be(2); // initial + coalesced rebuild
    }

    [TestMethod]
    public async Task Debounce_UsesCapturedSynchronizationContext()
    {
        // Arrange: use a tiny SyncContext that signals a TaskCompletionSource when Post is called
        var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        var ctx = new TestSyncCtx(() => tcs.TrySetResult(null));

        var original = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(ctx);
        try
        {
            var item = new ObservableItem(1);
            var source = new ObservableCollection<ObservableItem> { item };
            var builder = new RecordingPassThroughBuilder();

            var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(20) };
            opts.ObservedProperties.Add(nameof(ObservableItem.Value));
            using var view = FilteredObservableCollectionFactory.FromBuilder(source, _ => true, builder, opts);

            var collectionChangedCount = 0;
            view.CollectionChanged += (_, _) => collectionChangedCount++;

            // Act
            item.Value = 2;

            // Await that the debounce code posts to the captured context
            _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(1), this.TestContext.CancellationToken).ConfigureAwait(false);

            // Ensure the posted callback ran.
            await WaitUntilAsync(() => builder.BuildCount == 2, TimeSpan.FromSeconds(1), this.TestContext.CancellationToken).ConfigureAwait(false);

            // Assert minimal expectations: the context was posted to and the rebuild happened
            _ = ctx.PostCallCount.Should().Be(1);
            _ = collectionChangedCount.Should().Be(0);
            _ = builder.BuildCount.Should().Be(2); // initial + debounced rebuild
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(original);
        }
    }

    [TestMethod]
    public async Task Debounce_PredicatePath_RebuildsViaSharedPath()
    {
        // Arrange
        var item = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { item };

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(20) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromPredicate(
            source,
            i => i.Value % 2 == 0,
            opts);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        item.Value = 2;
        await Task.Delay(TimeSpan.FromMilliseconds(60), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert: Either a Reset or an incremental Add may be raised depending on implementation; the view must contain the new item.
        _ = events.Should().Contain(ev => ev.Action == NotifyCollectionChangedAction.Reset || ev.Action == NotifyCollectionChangedAction.Add);

        _ = view.Should().ContainSingle().Which.Value.Should().Be(2);
    }

    [TestMethod]
    public async Task Debounce_ZeroInterval_RebuildsEachChange()
    {
        // Arrange
        var item = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { item };
        var builder = new RecordingPassThroughBuilder();

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.Zero };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, _ => true, builder, opts);

        var collectionChangedCount = 0;
        view.CollectionChanged += (_, _) => collectionChangedCount++;

        // Act - changes should trigger immediate rebuilds
        item.Value = 2;
        item.Value = 3;

        // allow async handlers to run
        await Task.Delay(TimeSpan.FromMilliseconds(20), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = collectionChangedCount.Should().Be(0);
        _ = builder.BuildCount.Should().Be(3); // initial + two immediate rebuilds
    }

    private static async Task WaitUntilAsync(Func<bool> predicate, TimeSpan timeout, CancellationToken cancellationToken)
    {
        var start = Environment.TickCount64;
        while (!predicate())
        {
            if (TimeSpan.FromMilliseconds(Environment.TickCount64 - start) > timeout)
            {
                throw new TimeoutException("Condition was not met within the allotted time.");
            }

            await Task.Delay(TimeSpan.FromMilliseconds(5), cancellationToken).ConfigureAwait(false);
        }
    }

    private static void AssertSingleRemove(NotifyCollectionChangedEventArgs e, int oldStartingIndex, ObservableItem expectedItem)
    {
        _ = e.Action.Should().Be(NotifyCollectionChangedAction.Remove);
        _ = e.OldStartingIndex.Should().Be(oldStartingIndex);
        _ = e.OldItems.Should().NotBeNull();
        _ = e.OldItems!.Cast<ObservableItem>().Should().Equal([expectedItem]);
    }

    private static void AssertSingleAdd(NotifyCollectionChangedEventArgs e, int newStartingIndex, ObservableItem expectedItem)
    {
        _ = e.Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = e.NewStartingIndex.Should().Be(newStartingIndex);
        _ = e.NewItems.Should().NotBeNull();
        _ = e.NewItems!.Cast<ObservableItem>().Should().Equal([expectedItem]);
    }

    private static void AssertAddItems(NotifyCollectionChangedEventArgs e, int newStartingIndex, params ObservableItem[] expectedItems)
    {
        _ = e.Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = e.NewStartingIndex.Should().Be(newStartingIndex);
        _ = e.NewItems.Should().NotBeNull();
        _ = e.NewItems!.Cast<ObservableItem>().Should().Equal(expectedItems);
    }

    private sealed class RecordingPassThroughBuilder : IFilteredViewBuilder<ObservableItem>
    {
        public int BuildCount { get; private set; }

        public IReadOnlySet<ObservableItem> BuildForChangedItem(ObservableItem changedItem, bool becameIncluded, IReadOnlyList<ObservableItem> source)
        {
            this.BuildCount++;
            return new HashSet<ObservableItem>(); // No dependencies
        }
    }

    private sealed class ObservableItem(int value) : INotifyPropertyChanged, IEquatable<ObservableItem>
    {
        private int value = value;

        public event PropertyChangedEventHandler? PropertyChanged;

        public int Value
        {
            get => this.value;
            set
            {
                if (this.value == value)
                {
                    return;
                }

                this.value = value;
                this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.Value)));
            }
        }

        public bool Equals(ObservableItem? other) => ReferenceEquals(this, other);

        public override bool Equals(object? obj) => ReferenceEquals(this, obj);

        public override int GetHashCode() => this.value;

        public override string ToString() => $"Item({this.value})";
    }

    private sealed class TestSyncCtx(Action onPost) : SynchronizationContext
    {
        private readonly Action onPost = onPost;
        private int postCallCount;

        public int PostCallCount => this.postCallCount;

        public override void Post(SendOrPostCallback d, object? state)
        {
            _ = Interlocked.Increment(ref this.postCallCount);
            this.onPost();

            // Execute the posted callback synchronously so the test can observe the rebuild
            // without needing a separate pump step.
            d(state);
        }
    }
}
