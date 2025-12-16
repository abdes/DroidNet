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
    public async Task Debounce_CoalescesMultiplePropertyChanges()
    {
        // Arrange
        var item = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { item };
        var builder = new RecordingPassThroughBuilder();

        var opts = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(30) };
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, builder, opts);

        var resetCount = 0;
        view.CollectionChanged += (_, e) =>
        {
            if (e.Action == NotifyCollectionChangedAction.Reset)
            {
                resetCount++;
            }
        };

        // Act - two changes inside the debounce window
        item.Value = 2;
        await Task.Delay(TimeSpan.FromMilliseconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        item.Value = 3;

        // wait beyond debounce window for scheduled rebuild to fire
        await Task.Delay(TimeSpan.FromMilliseconds(80), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert - only one reset after coalescing
        _ = resetCount.Should().Be(1);
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
            using var view = FilteredObservableCollectionFactory.FromBuilder(source, builder, opts);

            var resetCount = 0;
            view.CollectionChanged += (_, e) =>
            {
                if (e.Action == NotifyCollectionChangedAction.Reset)
                {
                    resetCount++;
                }
            };

            // Act
            item.Value = 2;

            // Await that the debounce code posts to the captured context
            _ = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(1), this.TestContext.CancellationToken).ConfigureAwait(false);

            // Assert minimal expectations: the context was posted to and the rebuild happened
            _ = ctx.PostCallCount.Should().Be(1);
            _ = resetCount.Should().Be(1);
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
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, builder, opts);

        var resetCount = 0;
        view.CollectionChanged += (_, e) =>
        {
            if (e.Action == NotifyCollectionChangedAction.Reset)
            {
                resetCount++;
            }
        };

        // Act - changes should trigger immediate rebuilds
        item.Value = 2;
        item.Value = 3;

        // allow async handlers to run
        await Task.Delay(TimeSpan.FromMilliseconds(20), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = resetCount.Should().Be(2);
        _ = builder.BuildCount.Should().Be(3); // initial + two immediate rebuilds
    }

    private sealed class RecordingPassThroughBuilder : IFilteredViewBuilder<ObservableItem>
    {
        public int BuildCount { get; private set; }

        public IReadOnlyList<ObservableItem> Build(IReadOnlyList<ObservableItem> source)
        {
            this.BuildCount++;
            return [.. source];
        }
    }

    private sealed class ObservableItem(int value) : INotifyPropertyChanged
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
