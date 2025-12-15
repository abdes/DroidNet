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
    public void ObserveSourceChangesFalse_DoesNotAutoUpdateUntilRefresh()
    {
        // Arrange
        var source = new ObservableCollection<PlainItem> { new(1) };
        using var view = FilteredObservableCollectionFactory.FromPredicate(
            source,
            i => i.Value % 2 == 0,
            new FilteredObservableCollectionOptions
            {
                ObserveSourceChanges = false,
                ObserveItemChanges = false,
                RelevantProperties = null,
            });

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        source.Add(new PlainItem(2));

        // Assert
        _ = view.Should().BeEmpty();
        _ = events.Should().BeEmpty();

        // Act
        view.Refresh();

        // Assert
        _ = view.Should().ContainSingle();
        _ = view[0].Value.Should().Be(2);
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
    }

    [TestMethod]
    public void ObserveItemChangesFalse_DoesNotAutoUpdateUntilRefresh()
    {
        // Arrange
        var item = new NotifyingItem(1);
        var source = new ObservableCollection<NotifyingItem> { item };

        using var view = FilteredObservableCollectionFactory.FromPredicate(
            source,
            i => i.Value % 2 == 0,
            new FilteredObservableCollectionOptions
            {
                ObserveSourceChanges = true,
                ObserveItemChanges = false,
                RelevantProperties = null,
            });

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
