// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Text;
using System.Collections.Specialized;
using DroidNet.TestHelpers;
using FluentAssertions;

namespace DroidNet.Collections.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Filtered Observable Collection")]
public class FilteredObservableCollectionTests
{
    private sealed class ObservableItem : INotifyPropertyChanged
    {
        private int value;

        public ObservableItem(int value)
        {
            this.value = value;
        }

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

        public event PropertyChangedEventHandler? PropertyChanged;

        public override string ToString() => $"Item({this.value})";
    }

    [TestMethod]
    public void Constructor_NullSource_Throws()
    {
        // Arrange / Act
        var act = new Action(() => _ = new FilteredObservableCollection<ObservableItem>(null!, i => true));

        // Assert
        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void Constructor_NullFilter_Throws()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem>();

        // Act
        var act = new Action(() => _ = new FilteredObservableCollection<ObservableItem>(source, null!));

        // Assert
        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void Initialize_WithSourceItems_FiltersAndPreservesOrder()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem>
        {
            new ObservableItem(1),
            new ObservableItem(2),
            new ObservableItem(3),
        };

        // filter: include even values only
        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        // Assert
        _ = view.Count.Should().Be(1);
        _ = view.Should().ContainSingle().Which.Value.Should().Be(2);
    }

    [TestMethod]
    public void Source_Add_MatchingItem_RaisesAddEventAtCorrectIndex()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem>
        {
            new ObservableItem(1),
            new ObservableItem(2),
            new ObservableItem(3),
        };

        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        // Insert an even item between 1 and 2 in the source -> should appear before current 2 in the view
        source.Insert(1, new ObservableItem(4));

        // Assert
        _ = view.Count.Should().Be(2);
        _ = view[0].Value.Should().Be(4);
        _ = view[1].Value.Should().Be(2);

        _ = events.Should().HaveCount(1);
        _ = events[0].Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = events[0].NewItems![0].Should().Be(view[0]);
        _ = events[0].NewStartingIndex.Should().Be(0);
    }

    [TestMethod]
    public void Source_Add_NonMatchingItem_DoesNotRaiseEvent()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem>
        {
            new ObservableItem(1),
            new ObservableItem(2),
        };

        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        var raised = false;
        view.CollectionChanged += (_, _) => raised = true;

        // Act
        source.Add(new ObservableItem(5)); // odd

        // Assert
        _ = raised.Should().BeFalse();
        _ = view.Count.Should().Be(1);
    }

    [TestMethod]
    public void Source_Remove_ItemInFiltered_RaisesRemoveEventWithIndex()
    {
        // Arrange
        var a = new ObservableItem(2);
        var b = new ObservableItem(4);
        var source = new ObservableCollection<ObservableItem> { a, b };

        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        source.Remove(a);

        // Assert
        _ = view.Count.Should().Be(1);
        _ = view[0].Should().Be(b);
        _ = events.Should().HaveCount(1);
        _ = events[0].Action.Should().Be(NotifyCollectionChangedAction.Remove);
        _ = events[0].OldItems![0].Should().Be(a);
        _ = events[0].OldStartingIndex.Should().Be(0);
    }

    [TestMethod]
    public void Source_Move_ItemInFiltered_RaisesMoveEventWithCorrectIndices()
    {
        // Arrange
        var a = new ObservableItem(2); // filtered
        var b = new ObservableItem(3); // not
        var c = new ObservableItem(4); // filtered
        var source = new ObservableCollection<ObservableItem> { a, b, c };

        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        // view initially: [a, c]
        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act: move c (index 2) to position 0 in source -> view should become [c, a]
        source.Move(2, 0);

        // Assert
        _ = view.Count.Should().Be(2);
        _ = view[0].Should().Be(c);
        _ = view[1].Should().Be(a);

        _ = events.Should().HaveCount(1);
        _ = events[0].Action.Should().Be(NotifyCollectionChangedAction.Move);
        _ = events[0].NewItems![0].Should().Be(c);
        _ = events[0].NewStartingIndex.Should().Be(0);
        _ = events[0].OldStartingIndex.Should().Be(1);
    }

    [TestMethod]
    public void Source_Replace_OldRemovedAndNewAdded_RaisesRemoveAndAdd()
    {
        // Arrange
        var oldItem = new ObservableItem(2);
        var newItem = new ObservableItem(5);
        var source = new ObservableCollection<ObservableItem> { oldItem };

        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act: replace oldItem with newItem; newItem is odd so should not be in view
        source[0] = newItem;

        // Assert
        _ = view.Count.Should().Be(0);
        _ = events.Should().HaveCount(1);
        _ = events[0].Action.Should().Be(NotifyCollectionChangedAction.Remove);
    }

    [TestMethod]
    public void Source_Replace_NewMatches_RaisesAddOnly()
    {
        // Arrange - old item does not match, new item does
        var oldItem = new ObservableItem(1); // odd - not in view
        var newItem = new ObservableItem(2); // even - should be added
        var source = new ObservableCollection<ObservableItem> { oldItem };

        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);
        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        source[0] = newItem;

        // Assert
        _ = view.Count.Should().Be(1);
        _ = view[0].Should().Be(newItem);
        _ = events.Should().HaveCount(1);
        _ = events[0].Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = events[0].NewStartingIndex.Should().Be(0);
    }

    [TestMethod]
    public void Source_Replace_OldAndNewBothMatch_RaisesRemoveThenAdd_WithCorrectIndices()
    {
        // Arrange - both old and new match the filter
        var oldItem = new ObservableItem(2);
        var newItem = new ObservableItem(4);
        var source = new ObservableCollection<ObservableItem> { oldItem };

        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);
        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        source[0] = newItem;

        // Assert - should first remove old, then add new at same position
        _ = events.Should().HaveCount(2);
        _ = events[0].Action.Should().Be(NotifyCollectionChangedAction.Remove);
        _ = events[0].OldStartingIndex.Should().Be(0);
        _ = events[1].Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = events[1].NewStartingIndex.Should().Be(0);

        _ = view.Count.Should().Be(1);
        _ = view[0].Should().Be(newItem);
    }

    [TestMethod]
    public void Source_Reset_RebuildsAndRaisesReset()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem> { new ObservableItem(1), new ObservableItem(2) };
        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        source.Clear();
        source.Add(new ObservableItem(4));

        // Assert
        _ = view.Count.Should().Be(1);
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
    }

    [TestMethod]
    public void Item_PropertyChanged_TogglesInclusion_RaisesAddOrRemove()
    {
        // Arrange
        var item = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { item };
        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act: update item to even -> should be added
        item.Value = 2;

        // Assert add
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Add);
        events.Clear();

        // Act: update item back to odd -> should be removed
        item.Value = 3;

        // Assert remove
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Remove);
    }

    [TestMethod]
    public void RelevantProperties_FilterOnlyReevaluates_ForSpecifiedPropertyNames()
    {
        // Arrange
        var item = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { item };

        // Only changes to "Other" should cause re-evaluation — since we change Value, no event expected
        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0, new[] { "Other" });

        var raised = false;
        view.CollectionChanged += (_, _) => raised = true;

        // Act
        item.Value = 2;

        // Assert - no event because PropertyName not in relevantProperties
        _ = raised.Should().BeFalse();
    }

    [TestMethod]
    public void DeferNotifications_SuspendsEvents_ThenResetOnDispose()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem> { new ObservableItem(1) };
        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        using (view.DeferNotifications())
        {
            source.Add(new ObservableItem(2)); // would normally add
            source.Add(new ObservableItem(4));
            source.RemoveAt(0);
        }

        // Assert - only one Reset should be raised when defer disposed
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
    }

    [TestMethod]
    public void Refresh_RaisesResetAndLeavesViewCorrect()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem>
        {
            new ObservableItem(1),
            new ObservableItem(2),
            new ObservableItem(3),
        };

        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        view.Refresh();

        // Assert - Refresh always raises Reset even if nothing changed and view content remains correct
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
        _ = view.Count.Should().Be(1);
        _ = view[0].Value.Should().Be(2);
    }

    [TestMethod]
    public void Refresh_WhileNotificationsDeferred_RaisesSingleResetOnResume()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem>
        {
            new ObservableItem(1),
            new ObservableItem(2),
        };

        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);
        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act - defer notifications then call Refresh
        using (view.DeferNotifications())
        {
            view.Refresh();

            // while deferred, Refresh should not raise immediately
            _ = events.Should().BeEmpty();
        }

        // Assert - after disposing defer, a single Reset should be raised
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
    }

    [TestMethod]
    public void Refresh_AfterDispose_DoesNotRaise()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem> { new ObservableItem(2) };
        var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        var raised = false;
        view.CollectionChanged += (_, _) => raised = true;

        // Act
        view.Dispose();
        view.Refresh();

        // Assert
        _ = raised.Should().BeFalse();
    }

    [TestMethod]
    public void DeferNotifications_DisposeCalledMultipleTimes_SecondDisposeNoEffect()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem> { new ObservableItem(1) };
        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
    IDisposable d = view.DeferNotifications();

    // cause changes while suspended
    source.Add(new ObservableItem(2));

        // first dispose: should resume and raise Reset
        d.Dispose();

        // second dispose: should call ResumeNotifications with suspendCount <= 0 and do nothing / not throw
        var act = new Action(() => d.Dispose());

        // Assert
        _ = act.Should().NotThrow();
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
    }

    [TestMethod]
    public void Dispose_WhenCalledMultipleTimes_DoesNotThrow()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem> { new ObservableItem(2) };
        var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        // Act
        view.Dispose();
        var act = new Action(() => view.Dispose());

        // Assert
        _ = act.Should().NotThrow();
    }

    [TestMethod]
    public void Methods_AfterDispose_BehaveCorrectly()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem> { new ObservableItem(2) };
        using var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Dispose the view
        view.Dispose();

        // Act / Assert: Refresh does not throw and does not raise events
        var actRefresh = new Action(() => view.Refresh());
        _ = actRefresh.Should().NotThrow();
        _ = events.Should().BeEmpty();

        // DeferNotifications should throw ObjectDisposedException when called after dispose
        var actDefer = new Action(() => _ = view.DeferNotifications());
        _ = actDefer.Should().Throw<ObjectDisposedException>();

        // Count should be zero (Dispose cleared the view)
        _ = view.Count.Should().Be(0);

        // Enumerator should be empty
        _ = view.Should().BeEmpty();

        // Double dispose should not throw
        var actDoubleDispose = new Action(() => view.Dispose());
        _ = actDoubleDispose.Should().NotThrow();

        // Mutating the source after dispose should not raise events on the view
        source.Add(new ObservableItem(4));
        _ = events.Should().BeEmpty();
    }

    [TestMethod]
    public void Dispose_UnsubscribesFromSourceAndItems()
    {
        // Arrange
        var item = new ObservableItem(2);
        var source = new ObservableCollection<ObservableItem> { item };
        var view = new FilteredObservableCollection<ObservableItem>(source, i => i.Value % 2 == 0);

        // Ensure subscription exists
    var registered = EventHandlerTestHelper.FindAllRegisteredDelegates(source, "CollectionChanged");
    _ = registered.Should().Contain(x => x.Method.Name.Contains("OnSourceCollectionChanged") || x.Method.Name.Contains("OnSourceOnCollectionChanged"));

        // Act
        view.Dispose();

        // Assert source collection changed handler removed
    registered = EventHandlerTestHelper.FindAllRegisteredDelegates(source, "CollectionChanged");
    _ = registered.Should().NotContain(x => x.Method.Name.Contains("OnSourceCollectionChanged") || x.Method.Name.Contains("OnSourceOnCollectionChanged"));

        // item property changed handlers removed
        var itemHandlers = EventHandlerTestHelper.FindAllRegisteredDelegates(item, "PropertyChanged");
        _ = itemHandlers.Should().BeEmpty();
    }
}
