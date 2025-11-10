// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.Aura.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public class DispatcherCollectionProxyTests
{
    private static readonly string[] TestStringsAbc = ["a", "b", "c"];
    private static readonly string[] TestStringsFirstSecondThird = ["first", "second", "third"];
    private static readonly string[] TestStringsAb = ["a", "b"];
    private static readonly string[] TestStringsXyz = ["x", "y", "z"];
    private static readonly string[] TestStringsOneTwo = ["one", "two"];

    public TestContext TestContext { get; set; }

    [TestMethod]
    [SuppressMessage("Microsoft.Performance", "CA1806:DoNotIgnoreMethodResults", Justification = "Testing that constructor throws")]
    public void ConstructorThrowsOnNullSource()
        => _ = ((Action)(() => new DispatcherCollectionProxy<string>(null!)))
            .Should().Throw<ArgumentNullException>()
            .WithParameterName("source");

    [TestMethod]
    public void DelegatesCountToSource()
    {
        // Arrange
        var source = new List<string>(TestStringsAbc);
        using var proxy = new DispatcherCollectionProxy<string>(source);

        // Act & Assert
        _ = proxy.Count.Should().Be(3);
    }

    [TestMethod]
    public void DelegatesCountToIReadOnlyCollection()
    {
        // Arrange
        var source = new ReadOnlyCollection<string>([.. TestStringsAb]);
        using var proxy = new DispatcherCollectionProxy<string>(source);

        // Act & Assert
        _ = proxy.Count.Should().Be(2);
    }

    [TestMethod]
    public void DelegatesCountByEnumeration()
    {
        // Arrange
        var source = TestStringsXyz;
        using var proxy = new DispatcherCollectionProxy<string>(source);

        // Act & Assert
        _ = proxy.Count.Should().Be(3);
    }

    [TestMethod]
    public void DelegatesIndexerToSource()
    {
        // Arrange
        var source = new List<string>(TestStringsFirstSecondThird);
        using var proxy = new DispatcherCollectionProxy<string>(source);

        // Act & Assert
        _ = proxy[0].Should().Be("first");
        _ = proxy[1].Should().Be("second");
        _ = proxy[2].Should().Be("third");
    }

    [TestMethod]
    public void DelegatesIndexerToIReadOnlyList()
    {
        // Arrange
        var source = new ReadOnlyCollection<string>([.. TestStringsAb]);
        using var proxy = new DispatcherCollectionProxy<string>(source);

        // Act & Assert
        _ = proxy[0].Should().Be("a");
        _ = proxy[1].Should().Be("b");
    }

    [TestMethod]
    public void DelegatesIndexerByEnumeration()
    {
        // Arrange
        var source = TestStringsOneTwo;
        using var proxy = new DispatcherCollectionProxy<string>(source);

        // Act & Assert
        _ = proxy[0].Should().Be("one");
        _ = proxy[1].Should().Be("two");
    }

    [TestMethod]
    public void IndexerThrowsOnOutOfRange()
    {
        // Arrange
        var source = new List<string> { "item" };
        using var proxy = new DispatcherCollectionProxy<string>(source);

        // Act & Assert
        _ = ((Func<string>)(() => proxy[1]))
            .Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void DelegatesGetEnumeratorToSource()
    {
        // Arrange
        var source = new List<string> { "a", "b", "c" };
        using var proxy = new DispatcherCollectionProxy<string>(source);

        // Act
        var items = proxy.ToList();

        // Assert
        _ = items.Should().Equal("a", "b", "c");
    }

    [TestMethod]
    public async Task ForwardsCollectionChangedEvents()
    {
        // Arrange
        var source = new ObservableCollection<string> { "initial" };
        using var proxy = new DispatcherCollectionProxy<string>(source, source);

        NotifyCollectionChangedEventArgs? receivedArgs = null;
        proxy.CollectionChanged += (s, e) => receivedArgs = e;

        // Act
        source.Add("new item");
        await Task.Delay(10, this.TestContext.CancellationToken).ConfigureAwait(true); // Allow event to propagate

        // Assert
        _ = receivedArgs.Should().NotBeNull();
        _ = receivedArgs!.Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = receivedArgs.NewItems!.Cast<string>().Should().Contain("new item");
    }

    [TestMethod]
    public async Task UsesTryEnqueueWhenProvided()
    {
        // Arrange
        var source = new ObservableCollection<string> { "initial" };
        var enqueued = false;
        Action? enqueuedAction = null;
        var tryEnqueue = new Func<Action, bool>(action =>
        {
            enqueued = true;
            enqueuedAction = action;
            return true;
        });

        using var proxy = new DispatcherCollectionProxy<string>(source, source, tryEnqueue);

        NotifyCollectionChangedEventArgs? receivedArgs = null;
        proxy.CollectionChanged += (s, e) => receivedArgs = e;

        // Act
        source.Add("new item");
        await Task.Delay(10, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = enqueued.Should().BeTrue("tryEnqueue should be called");
        _ = receivedArgs.Should().BeNull("Event should not be raised yet");

        // Act - Execute the enqueued action
        enqueuedAction!();
        await Task.Delay(10, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = receivedArgs.Should().NotBeNull();
        _ = receivedArgs!.Action.Should().Be(NotifyCollectionChangedAction.Add);
    }

    [TestMethod]
    public async Task RaisesInlineWhenTryEnqueueFails()
    {
        // Arrange
        var source = new ObservableCollection<string> { "initial" };
        var tryEnqueue = new Func<Action, bool>(_ => false); // Always fails

        using var proxy = new DispatcherCollectionProxy<string>(source, source, tryEnqueue);

        NotifyCollectionChangedEventArgs? receivedArgs = null;
        proxy.CollectionChanged += (s, e) => receivedArgs = e;

        // Act
        source.Add("new item");
        await Task.Delay(10, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = receivedArgs.Should().NotBeNull("Event should be raised inline when enqueue fails");
        _ = receivedArgs!.Action.Should().Be(NotifyCollectionChangedAction.Add);
    }

    [TestMethod]
    public async Task DisposeUnsubscribesFromSourceNotifier()
    {
        // Arrange
        var source = new ObservableCollection<string> { "initial" };
        var proxy = new DispatcherCollectionProxy<string>(source, source);

        NotifyCollectionChangedEventArgs? receivedArgs = null;
        proxy.CollectionChanged += (s, e) => receivedArgs = e;

        // Act - Dispose
        proxy.Dispose();

        // Act - Add item after dispose
        source.Add("after dispose");
        await Task.Delay(10, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = receivedArgs.Should().BeNull("No event should be raised after dispose");
    }

    [TestMethod]
    public void DisposeIsIdempotent()
    {
        // Arrange
        var source = new ObservableCollection<string>();
        var proxy = new DispatcherCollectionProxy<string>(source, source);

        // Act
        proxy.Dispose();
        proxy.Dispose(); // Second dispose should not throw

        // Assert - No exception
    }

    [TestMethod]
    public async Task DoesNotForwardEventsAfterDispose()
    {
        // Arrange
        var source = new ObservableCollection<string> { "initial" };
        var proxy = new DispatcherCollectionProxy<string>(source, source);

        NotifyCollectionChangedEventArgs? receivedArgs = null;
        proxy.CollectionChanged += (s, e) => receivedArgs = e;

        // Act - Dispose
        proxy.Dispose();

        // Act - Trigger event
        source.Clear();
        await Task.Delay(10, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = receivedArgs.Should().BeNull();
    }
}
