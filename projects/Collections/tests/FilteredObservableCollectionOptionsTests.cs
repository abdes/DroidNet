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
[TestCategory("Options")]
public class FilteredObservableCollectionOptionsTests
{
    [TestMethod]
    public void Constructor_AllowsNonNotifyPropertyChangedItems()
    {
        // Arrange
        var source = new ObservableCollection<PlainItem>
        {
            new(1),
            new(2),
            new(3),
        };

        // Act
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0);

        // Assert
        _ = view.Should().ContainSingle();
        _ = view[0].Value.Should().Be(2);
    }

    [TestMethod]
    public void SourceChanges_AreAlwaysObserved()
    {
        // Arrange
        var source = new ObservableCollection<PlainItem> { new(1) };
        using var view = FilteredObservableCollectionFactory.FromPredicate(
            source,
            i => i.Value % 2 == 0,
            new FilteredObservableCollectionOptions());

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act: add an item that matches the predicate
        source.Add(new PlainItem(2));

        // Assert: the view should update automatically (incremental Add or Reset depending on builder)
        _ = view.Should().ContainSingle();
        _ = view[0].Value.Should().Be(2);
        _ = events.Should().NotBeEmpty();
    }

    [TestMethod]
    public void ObserveItemChangesFalse_DoesNotAutoUpdateUntilRefresh()
    {
        // Arrange
        var item = new NotifyingItem(1);
        var source = new ObservableCollection<NotifyingItem> { item };
        var opts = new FilteredObservableCollectionOptions();
        using var view = FilteredObservableCollectionFactory.FromPredicate(source, i => i.Value % 2 == 0, opts);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        item.Value = 2;

        // Assert
        _ = view.Should().BeEmpty();
        _ = events.Should().BeEmpty();

        // Act
        view.Refresh();

        // Assert
        _ = view.Should().ContainSingle();
        _ = view[0].Should().BeSameAs(item);
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
    }

    [TestMethod]
    public void ObservedProperties_CanBeChanged_EnablesItemObservation()
    {
        // Arrange
        var opts = new FilteredObservableCollectionOptions();
        var item = new NotifyingItem(1);
        var source = new ObservableCollection<NotifyingItem> { item };

        using var view = FilteredObservableCollectionFactory.FromPredicate(
            source,
            i => i.Value % 2 == 0,
            opts);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act - no observation yet
        item.Value = 2;
        _ = events.Should().BeEmpty();

        // Enable observation for Value
        opts.ObservedProperties!.Add(nameof(NotifyingItem.Value));

        // Act - now changes to Value should be observed
        item.Value = 3;
        item.Value = 4;

        // Assert - an update should have been observed (a Reset is acceptable)
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
        _ = view.Should().ContainSingle().Which.Should().BeSameAs(item);
    }

    [TestMethod]
    public void ObservedProperties_Clearing_DisablesObservation()
    {
        // Arrange
        var opts = new FilteredObservableCollectionOptions();
        opts.ObservedProperties.Add(nameof(NotifyingItem.Value));
        var item = new NotifyingItem(1);
        var source = new ObservableCollection<NotifyingItem> { item };

        using var view = FilteredObservableCollectionFactory.FromPredicate(
            source,
            i => i.Value % 2 == 0,
            opts);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act - initial change observed
        item.Value = 2;
        _ = events.Should().NotBeEmpty();
        _ = view.Should().ContainSingle().Which.Should().BeSameAs(item);

        // Clear observed properties - should stop observing further changes
        events.Clear();
        opts.ObservedProperties!.Clear();

        // Act - now changes to Value should NOT be observed
        item.Value = 3;
        _ = events.Should().BeEmpty();
    }

    [TestMethod]
    public void ObservedProperties_AddRemove_TransitionsObservation()
    {
        // Arrange
        var opts = new FilteredObservableCollectionOptions();
        var item = new NotifyingItem(1);
        var source = new ObservableCollection<NotifyingItem> { item };

        using var view = FilteredObservableCollectionFactory.FromPredicate(
            source,
            i => i.Value % 2 == 0,
            opts);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Add observation for Value - this causes an immediate Reset but does not add the item yet
        opts.ObservedProperties!.Add(nameof(NotifyingItem.Value));
        _ = events.Should().NotBeEmpty();
        _ = view.Should().BeEmpty();

        // Now changes are observed
        events.Clear();
        item.Value = 2;
        _ = events.Should().NotBeEmpty();
        _ = view.Should().ContainSingle().Which.Should().BeSameAs(item);

        // Removing property should stop observation
        events.Clear();
        _ = opts.ObservedProperties.Remove(nameof(NotifyingItem.Value));
        item.Value = 3;
        _ = events.Should().BeEmpty();
    }

    [TestMethod]
    public void NoObservation_ItemPropertyChangesIgnored_ButSourceAddsObserve()
    {
        // Arrange: an explicit empty ObservedProperties collection means we ignore item property changes
        var opts = new FilteredObservableCollectionOptions();
        var item = new NotifyingItem(1);
        var newItem = new NotifyingItem(2);
        var source = new ObservableCollection<NotifyingItem> { item };

        using var view = FilteredObservableCollectionFactory.FromPredicate(
            source,
            i => i.Value % 2 == 0,
            opts);

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act - perform changes that would normally be observed
        source.Add(newItem);
        item.Value = 2; // change existing item so it would match the filter

        // Assert - source add should have been observed, item property change should NOT be observed automatically
        _ = events.Should().NotBeEmpty(); // source add causes an event (Add or Reset)
        _ = view.Should().ContainSingle().Which.Should().BeSameAs(newItem);

        // Act - manual refresh picks up the item property change
        events.Clear();
        view.Refresh();

        // Assert - a single Reset (or equivalent full update) should be raised and view should contain both matching items
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
        _ = view.Should().HaveCount(2);
        _ = view.Should().Contain(item).And.Contain(newItem);
    }

    private sealed class PlainItem(int value)
    {
        public int Value { get; } = value;

        public override string ToString() => $"PlainItem({this.Value})";
    }

    private sealed class NotifyingItem(int value) : INotifyPropertyChanged
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

        public override string ToString() => $"NotifyingItem({this.value})";
    }
}
