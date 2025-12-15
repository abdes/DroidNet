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
[TestCategory("Builder")]
public class FilteredObservableCollectionBuilderTests
{
    private static readonly string[] Options = ["Other"];

    [TestMethod]
    public void Constructor_NullBuilder_Throws()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem>();

        // Act
        var act = new Action(() => _ = FilteredObservableCollectionFactory.FromBuilder<ObservableItem>(source, null!));

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
        using var view = FilteredObservableCollectionFactory.FromBuilder(source, builder);

        // Assert
        _ = view.Should().ContainSingle();
        _ = view[0].Value.Should().Be(2);
        _ = builder.BuildCount.Should().Be(1);
    }

    [TestMethod]
    public void Builder_RelevantPropertyChange_TriggersReset()
    {
        // Arrange
        var item = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { item };
        var builder = new RecordingEvenBuilder();

        using var view = FilteredObservableCollectionFactory.FromBuilder(
            source,
            builder,
            new FilteredObservableCollectionOptions
            {
                RelevantProperties = [nameof(ObservableItem.Value)],
                ObserveSourceChanges = true,
                ObserveItemChanges = true,
                PropertyChangedDebounceInterval = TimeSpan.Zero,
            });

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        item.Value = 2; // becomes even -> should trigger rebuild/reset

        // Assert
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
        _ = builder.BuildCount.Should().Be(2); // initial + after property change
    }

    [TestMethod]
    public void Builder_IrrelevantPropertyChange_DoesNotTriggerReset()
    {
        // Arrange
        var item = new ObservableItem(1);
        var source = new ObservableCollection<ObservableItem> { item };
        var builder = new RecordingEvenBuilder();

        using var view = FilteredObservableCollectionFactory.FromBuilder(
            source,
            builder,
            new FilteredObservableCollectionOptions
            {
                RelevantProperties = Options,
                ObserveSourceChanges = true,
                ObserveItemChanges = true,
                PropertyChangedDebounceInterval = TimeSpan.Zero,
            });

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        item.Value = 2; // not in relevantProperties

        // Assert
        _ = events.Should().BeEmpty();
        _ = builder.BuildCount.Should().Be(1); // only initial build
    }

    [TestMethod]
    public void Builder_SourceAdd_RaisesResetAndRebuilds()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem> { new(1) };
        var builder = new RecordingEvenBuilder();

        using var view = FilteredObservableCollectionFactory.FromBuilder(
            source,
            builder,
            new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.Zero });

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        source.Add(new ObservableItem(2));

        // Assert
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
        _ = view.Should().ContainSingle().Which.Value.Should().Be(2);
        _ = builder.BuildCount.Should().Be(2); // initial + after add
    }

    [TestMethod]
    public void Builder_SourceRemove_RaisesResetAndRebuilds()
    {
        // Arrange
        var even = new ObservableItem(2);
        var source = new ObservableCollection<ObservableItem> { even, new(3) };
        var builder = new RecordingEvenBuilder();

        using var view = FilteredObservableCollectionFactory.FromBuilder(
            source,
            builder,
            new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.Zero });

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        _ = source.Remove(even);

        // Assert
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
        _ = view.Should().BeEmpty();
        _ = builder.BuildCount.Should().Be(2); // initial + after remove
    }

    [TestMethod]
    public void Builder_SourceReplace_RaisesResetAndRebuilds()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem> { new(2) };
        var builder = new RecordingEvenBuilder();

        using var view = FilteredObservableCollectionFactory.FromBuilder(
            source,
            builder,
            new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.Zero });

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        source[0] = new ObservableItem(4);

        // Assert
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
        _ = view.Should().ContainSingle().Which.Value.Should().Be(4);
        _ = builder.BuildCount.Should().Be(2); // initial + after replace
    }

    [TestMethod]
    public void Builder_SourceMove_RaisesResetAndRebuilds()
    {
        // Arrange
        var first = new ObservableItem(2);
        var second = new ObservableItem(4);
        var source = new ObservableCollection<ObservableItem> { first, second };
        var builder = new RecordingEvenBuilder();

        using var view = FilteredObservableCollectionFactory.FromBuilder(
            source,
            builder,
            new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.Zero });

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        source.Move(1, 0);

        // Assert
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
        _ = view.Should().HaveCount(2);
        _ = view.Should().HaveElementAt(0, second);
        _ = view.Should().HaveElementAt(1, first);
        _ = builder.BuildCount.Should().Be(2); // initial + after move
    }

    [TestMethod]
    public void Builder_Refresh_RaisesResetAndRebuilds()
    {
        // Arrange
        var source = new ObservableCollection<ObservableItem> { new(1), new(2) };
        var builder = new RecordingEvenBuilder();

        using var view = FilteredObservableCollectionFactory.FromBuilder(
            source,
            builder,
            new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.Zero });

        var events = new List<NotifyCollectionChangedEventArgs>();
        view.CollectionChanged += (_, e) => events.Add(e);

        // Act
        view.Refresh();

        // Assert
        _ = events.Should().ContainSingle(e => e.Action == NotifyCollectionChangedAction.Reset);
        _ = view.Should().ContainSingle().Which.Value.Should().Be(2);
        _ = builder.BuildCount.Should().Be(2); // initial + refresh
    }

    private sealed class RecordingEvenBuilder : IFilteredViewBuilder<ObservableItem>
    {
        public int BuildCount { get; private set; }

        public IReadOnlyList<ObservableItem> Build(IReadOnlyList<ObservableItem> source)
        {
            this.BuildCount++;
            return [.. source.Where(i => i.Value % 2 == 0)];
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
}
