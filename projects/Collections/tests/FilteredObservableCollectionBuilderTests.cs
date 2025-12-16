// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using AwesomeAssertions;

namespace DroidNet.Collections.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Filtered Observable Collection")]
[TestCategory("Builder")]
public class FilteredObservableCollectionBuilderTests
{
    [TestMethod]
    public void Builder_PropertyChange_WhenBuilderThrows_DoesNotPartiallyMutate_AndSubsequentValidUpdateWorks()
    {
        // Arrange
        var trigger = new ObservableItem(0);
        var source = new ObservableCollection<ObservableItem> { trigger };

        var builder = new ToggleThrowBuilder(throwCount: 1, invalidDependencyCount: 0);

        var opts = new FilteredObservableCollectionOptions();
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, i => i.Value == 1, builder, opts);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        _ = view.Should().BeEmpty();

        // Act + Assert: builder throws; view must remain unchanged and no partial notifications should be raised.
        _ = new Action(() => trigger.Value = 1).Should().Throw<InvalidOperationException>();
        _ = view.Should().BeEmpty();
        _ = events.Should().BeEmpty();

        // Act: move away and back; second include must succeed.
        trigger.Value = 2; // no-op for view
        trigger.Value = 1;

        // Assert: view updates correctly after the earlier exception.
        _ = view.Should().ContainSingle().Which.Should().BeSameAs(trigger);
        _ = events.Should().ContainSingle().Which.Action.Should().Be(NotifyCollectionChangedAction.Add);
    }

    [TestMethod]
    public void Builder_PropertyChange_WhenBuilderReturnsInvalidDependency_DoesNotPartiallyMutate_AndSubsequentValidUpdateWorks()
    {
        // Arrange
        var trigger = new ObservableItem(0);
        var source = new ObservableCollection<ObservableItem> { trigger };

        var builder = new ToggleThrowBuilder(throwCount: 0, invalidDependencyCount: 1);

        var opts = new FilteredObservableCollectionOptions();
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, i => i.Value == 1, builder, opts);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        _ = view.Should().BeEmpty();

        // Act + Assert: invalid builder dependency should throw; view remains unchanged.
        _ = new Action(() => trigger.Value = 1).Should().Throw<InvalidOperationException>();
        _ = view.Should().BeEmpty();
        _ = events.Should().BeEmpty();

        // Act: move away and back; second include must succeed.
        trigger.Value = 2;
        trigger.Value = 1;

        // Assert
        _ = view.Should().ContainSingle().Which.Should().BeSameAs(trigger);
        _ = events.Should().ContainSingle().Which.Action.Should().Be(NotifyCollectionChangedAction.Add);
    }

    [TestMethod]
    public void Constructor_NullBuilder_Throws()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem>();

        // Act
        var act = new Action(() => _ = FilteredObservableCollectionFactory.FromBuilder<ObservableItem>(source, _ => true, null!));

        // Assert
        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void Builder_InitialBuild_UsesBuilderProjection()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem>
        {
            new(1),
            new(2),
            new(3),
        };

        var builder = new RecordingEvenBuilder();

        // Act
        // Predicate: "is even". Builder: "if included, include itself".
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, i => i.Value % 2 == 0, builder);

        _ = view.Should().ContainSingle();
        _ = view[0].Value.Should().Be(2);
    }

    [TestMethod]
    public void Builder_RelevantPropertyChange_TriggersDeltaUpdate()
    {
        // Arrange
        var item = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { item };
        var builder = new RecordingEvenBuilder();

        var opts = new FilteredObservableCollectionOptions();
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromBuilder(
            source,
            i => i.Value % 2 == 0,
            builder,
            opts);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        item.Value = 2; // becomes even -> Included

        // Assert
        _ = events.Should().ContainSingle().Which.Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = view.Should().ContainSingle().Which.Value.Should().Be(2);
        _ = builder.BuildCount.Should().Be(1); // 1 call for property change? (Initial build calls? "Initial state" logic?)
    }

    [TestMethod]
    public void Builder_IrrelevantPropertyChange_DoesNotTriggerUpdate()
    {
        // Arrange
        var item = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { item };
        var builder = new RecordingEvenBuilder();

        var opts = new FilteredObservableCollectionOptions();
        opts.ObservedProperties.Add("Other");

        using var view = FilteredObservableCollectionFactory.FromBuilder(
            source,
            i => i.Value % 2 == 0, // Predicate checks Value
            builder,
            opts);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        item.Value = 2; // Not observed

        // Assert
        _ = events.Should().BeEmpty();
        _ = builder.BuildCount.Should().Be(0); // Not called
    }

    [TestMethod]
    public void Builder_SourceAdd_AddsItem()
    {
        var source = new ObservableCollection<ObservableItem> { new(1) };
        var builder = new RecordingEvenBuilder();
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, i => i.Value % 2 == 0, builder);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        var newItem = new ObservableItem(2);
        source.Add(newItem);

        // Assert
        _ = events.Should().ContainSingle().Which.Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = view.Should().ContainSingle().Which.Should().BeSameAs(newItem);
    }

    [TestMethod]
    public void Builder_SourceRemove_RemovesItem()
    {
        var item = new ObservableItem(2);
        var source = new ObservableCollection<ObservableItem> { item };
        var builder = new RecordingEvenBuilder();
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, i => i.Value % 2 == 0, builder);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        _ = source.Remove(item);

        // Assert
        _ = events.Should().ContainSingle().Which.Action.Should().Be(NotifyCollectionChangedAction.Remove);
        _ = view.Should().BeEmpty();
    }

    [TestMethod]
    public void Builder_SourceRemove_RemovingReferencedDependency_ThrowsAndKeepsViewConsistent()
    {
        // Arrange
        var dep = new ObservableItem(0);
        var trigger = new ObservableItem(0);
        var source = new ObservableCollection<ObservableItem> { dep, trigger };

        var builder = new ConditionalDependencyBuilder(dep);

        var opts = new FilteredObservableCollectionOptions();
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, i => i.Value == 1, builder, opts);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Make trigger included; dep becomes included as a dependency.
        trigger.Value = 1;
        _ = view.Should().Equal([dep, trigger]);
        events.Clear();

        // Act
        var act = new Action(() => source.Remove(dep));

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
        _ = events.Should().BeEmpty();
        _ = view.Should().Equal([trigger]);

        // Subsequent changes should still work and should not reintroduce the removed dependency.
        events.Clear();
        trigger.Value = 0;
        _ = view.Should().BeEmpty();
        _ = events.Should().ContainSingle().Which.Action.Should().Be(NotifyCollectionChangedAction.Remove);
    }

    [TestMethod]
    public void Builder_SourceMove_MovesItem()
    {
        // Move is handled by Tree structure updates, not builder.
        // But we should verify correct event.
        var i2 = new ObservableItem(2);
        var i4 = new ObservableItem(4);
        var source = new ObservableCollection<ObservableItem> { i2, i4 }; // Both even, included
        var builder = new RecordingEvenBuilder();
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, i => i.Value % 2 == 0, builder);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        source.Move(0, 1); // Swap

        // Assert
        _ = events.Should().ContainSingle().Which.Action.Should().Be(NotifyCollectionChangedAction.Move);
        _ = view[0].Should().BeSameAs(i4);
        _ = view[1].Should().BeSameAs(i2);
    }

    [TestMethod]
    [SuppressMessage("Design", "MA0051:Method is too long", Justification = "the test is complex")]
    public void Builder_PropertyChange_AddsMultipleItems_ViewAndEventPreserveSourceOrder()
    {
        // Arrange
        var a = new ObservableItem(0);
        var b = new ObservableItem(0);
        var trigger = new ObservableItem(0);
        var d = new ObservableItem(0);
        var e = new ObservableItem(0);
        var source = new ObservableCollection<ObservableItem> { a, b, trigger, d, e };

        var builder = new TriggerDependencyBuilder(trigger, [e, a, d]);

        var opts = new FilteredObservableCollectionOptions();
        opts.ObservedProperties.Add(nameof(ObservableItem.Value));
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, i => i.Value == 1, builder, opts);

        _ = view.Should().BeEmpty();

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e2) => events.Add(e2);

        // Act - make trigger included; builder returns dependencies in a non-source order.
        trigger.Value = 1;

        // Assert - view order must follow source order, not builder enumeration order.
        _ = view.Should().HaveCount(4);
        _ = view[0].Should().BeSameAs(a);
        _ = view[1].Should().BeSameAs(trigger);
        _ = view[2].Should().BeSameAs(d);
        _ = view[3].Should().BeSameAs(e);

        _ = events.Should().OnlyContain(e2 => e2.Action != NotifyCollectionChangedAction.Reset);
        _ = events.Should().ContainSingle();
        _ = events[0].Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = events[0].NewStartingIndex.Should().Be(0);
        _ = events[0].NewItems.Should().NotBeNull();
        _ = events[0].NewItems!.Count.Should().Be(4);
        _ = events[0].NewItems![0].Should().BeSameAs(a);
        _ = events[0].NewItems![1].Should().BeSameAs(trigger);
        _ = events[0].NewItems![2].Should().BeSameAs(d);
        _ = events[0].NewItems![3].Should().BeSameAs(e);

        // Act - now exclude trigger again.
        events.Clear();
        trigger.Value = 0;

        // Assert - removals must also preserve source order in OldItems.
        _ = view.Should().BeEmpty();
        _ = events.Should().OnlyContain(e2 => e2.Action != NotifyCollectionChangedAction.Reset);
        _ = events.Should().ContainSingle();
        _ = events[0].Action.Should().Be(NotifyCollectionChangedAction.Remove);
        _ = events[0].OldStartingIndex.Should().Be(0);
        _ = events[0].OldItems.Should().NotBeNull();
        _ = events[0].OldItems!.Count.Should().Be(4);
        _ = events[0].OldItems![0].Should().BeSameAs(a);
        _ = events[0].OldItems![1].Should().BeSameAs(trigger);
        _ = events[0].OldItems![2].Should().BeSameAs(d);
        _ = events[0].OldItems![3].Should().BeSameAs(e);
    }

    [TestMethod]
    public void ReevaluatePredicate_WithBuilder_AddsAndRemovesIncrementallyWithoutReset()
    {
        // Arrange
        var dep = new ObservableItem(0);
        var trigger = new ObservableItem(0);
        var source = new ObservableCollection<ObservableItem> { dep, trigger };

        var enabled = false;
        var builder = new TriggerDependencyBuilder(trigger, [dep]);
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, _ => enabled, builder);

        _ = view.Should().BeEmpty();

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act: enable predicate, then re-evaluate
        enabled = true;
        view.ReevaluatePredicate();

        // Assert: added dep + trigger, no Reset
        _ = events.Should().OnlyContain(e => e.Action != NotifyCollectionChangedAction.Reset);
        _ = view.Should().Equal([dep, trigger]);
        _ = events.Should().ContainSingle();
        _ = events[0].Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = events[0].NewStartingIndex.Should().Be(0);
        _ = events[0].NewItems.Should().NotBeNull();
        _ = events[0].NewItems!.Cast<ObservableItem>().Should().Equal([dep, trigger]);

        // Act: disable predicate, then re-evaluate
        events.Clear();
        enabled = false;
        view.ReevaluatePredicate();

        // Assert: removed dep + trigger, no Reset
        _ = events.Should().OnlyContain(e => e.Action != NotifyCollectionChangedAction.Reset);
        _ = view.Should().BeEmpty();
        _ = events.Should().ContainSingle();
        _ = events[0].Action.Should().Be(NotifyCollectionChangedAction.Remove);
        _ = events[0].OldStartingIndex.Should().Be(0);
        _ = events[0].OldItems.Should().NotBeNull();
        _ = events[0].OldItems!.Cast<ObservableItem>().Should().Equal([dep, trigger]);
    }

    // Helper builder that just returns the item itself if included.
    // This effectively tests the RefCount=2 scenario (1 from Predicate, 1 from Builder).
    private sealed class RecordingEvenBuilder : IFilteredViewBuilder<ObservableItem>
    {
        public int BuildCount { get; private set; }

        public IReadOnlySet<ObservableItem> BuildForChangedItem(ObservableItem changedItem, bool becameIncluded, IReadOnlyList<ObservableItem> source)
        {
            this.BuildCount++;
            var set = new HashSet<ObservableItem>(ReferenceEqualityComparer.Instance)
            {
                changedItem,
            };
            return set;
        }
    }

    private sealed class TriggerDependencyBuilder(ObservableItem trigger, IReadOnlyList<ObservableItem> dependencies) : IFilteredViewBuilder<ObservableItem>
    {
        public IReadOnlySet<ObservableItem> BuildForChangedItem(ObservableItem changedItem, bool becameIncluded, IReadOnlyList<ObservableItem> source)
        {
            if (!ReferenceEquals(changedItem, trigger))
            {
                return new HashSet<ObservableItem>(ReferenceEqualityComparer.Instance);
            }

            var set = new HashSet<ObservableItem>(ReferenceEqualityComparer.Instance);
            foreach (var dep in dependencies)
            {
                set.Add(dep);
            }

            return set;
        }
    }

    private sealed class ToggleThrowBuilder(int throwCount, int invalidDependencyCount) : IFilteredViewBuilder<ObservableItem>
    {
        private int throwCount = throwCount;
        private int invalidDependencyCount = invalidDependencyCount;

        public IReadOnlySet<ObservableItem> BuildForChangedItem(ObservableItem changedItem, bool becameIncluded, IReadOnlyList<ObservableItem> source)
        {
            if (this.throwCount > 0)
            {
                this.throwCount--;
                throw new InvalidOperationException("Builder failure");
            }

            var set = new HashSet<ObservableItem>(ReferenceEqualityComparer.Instance);

            if (this.invalidDependencyCount > 0)
            {
                this.invalidDependencyCount--;
                set.Add(new ObservableItem(123));
            }

            return set;
        }
    }

    private sealed class ConditionalDependencyBuilder(ObservableItem dependency) : IFilteredViewBuilder<ObservableItem>
    {
        public IReadOnlySet<ObservableItem> BuildForChangedItem(ObservableItem changedItem, bool becameIncluded, IReadOnlyList<ObservableItem> source)
        {
            var set = new HashSet<ObservableItem>(ReferenceEqualityComparer.Instance);
            if (source.Contains(dependency))
            {
                set.Add(dependency);
            }

            return set;
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
}
