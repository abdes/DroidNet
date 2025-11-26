// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Globalization;
using AwesomeAssertions;
using DroidNet.Controls.OutputConsole.Model;

namespace Controls.OutputConsole.Tests;

[TestClass]
public class OutputLogBufferTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public void Constructor_WithValidCapacity_ShouldInitialize()
    {
        // Arrange
        const int capacity = 100;

        // Act
        var buffer = new OutputLogBuffer(capacity);

        // Assert
        _ = buffer.Capacity.Should().Be(capacity);
        _ = buffer.Count.Should().Be(0);
        _ = buffer.IsPaused.Should().BeFalse();
    }

    [TestMethod]
    public void Constructor_WithInvalidCapacity_ShouldThrow()
    {
        // Arrange
        const int capacity = 0;

        // Act
        static void Act() => _ = new OutputLogBuffer(capacity);

        // Assert
        _ = ((Action)Act).Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void Append_ShouldAddItem()
    {
        // Arrange
        var buffer = new OutputLogBuffer(10);
        var entry = new OutputLogEntry { Message = "Test" };

        // Act
        buffer.Append(entry);

        // Assert
        _ = buffer.Should().ContainSingle().Which.Should().Be(entry);
    }

    [TestMethod]
    public void Append_WhenCapacityExceeded_ShouldRemoveOldest()
    {
        // Arrange
        var buffer = new OutputLogBuffer(2);
        var entry1 = new OutputLogEntry { Message = "1" };
        var entry2 = new OutputLogEntry { Message = "2" };
        var entry3 = new OutputLogEntry { Message = "3" };

        // Act
        buffer.Append(entry1);
        buffer.Append(entry2);
        buffer.Append(entry3);

        // Assert
        _ = buffer.Should().HaveCount(2);
        _ = buffer.Should().ContainInOrder(entry2, entry3);
        _ = buffer.Should().NotContain(entry1);
    }

    [TestMethod]
    public void Clear_ShouldRemoveAllItems()
    {
        // Arrange
        var buffer = new OutputLogBuffer(10);
        buffer.Append(new OutputLogEntry());

        // Act
        buffer.Clear();

        // Assert
        _ = buffer.Should().BeEmpty();
    }

    [TestMethod]
    public void IsPaused_WhenSetToTrue_ShouldSuppressNotifications()
    {
        // Arrange
        var buffer = new OutputLogBuffer(10);
        var collectionChanged = false;
        buffer.CollectionChanged += (_, _) => collectionChanged = true;

        // Act
        buffer.IsPaused = true;
        buffer.Append(new OutputLogEntry());

        // Assert
        _ = collectionChanged.Should().BeFalse();
        _ = buffer.Should().ContainSingle();
    }

    [TestMethod]
    public void IsPaused_WhenSetToFalse_ShouldRaiseReset()
    {
        // Arrange
        var buffer = new OutputLogBuffer(10)
        {
            IsPaused = true,
        };
        var resetRaised = false;
        buffer.CollectionChanged += (_, e) =>
        {
            if (e.Action == NotifyCollectionChangedAction.Reset)
            {
                resetRaised = true;
            }
        };

        // Act
        buffer.IsPaused = false;

        // Assert
        _ = resetRaised.Should().BeTrue();
    }

    [TestMethod]
    public void Append_ConcurrentAccess_ShouldBeThreadSafe()
    {
        // Arrange
        const int capacity = 1000;
        const int threadCount = 10;
        const int itemsPerThread = 100;
        var buffer = new OutputLogBuffer(capacity);
        var tasks = new List<Task>();

        // Act
        for (var i = 0; i < threadCount; i++)
        {
            tasks.Add(Task.Run(
                () =>
                {
                    for (var j = 0; j < itemsPerThread; j++)
                    {
                        buffer.Append(new OutputLogEntry
                        {
                            Message = string.Create(CultureInfo.InvariantCulture, $"Message {j}"),
                        });
                    }
                },
                this.TestContext.CancellationToken));
        }

        Task.WaitAll([.. tasks]);

        // Assert
        _ = buffer.Count.Should().Be(threadCount * itemsPerThread);
    }

    [TestMethod]
    public void Append_ConcurrentAccess_WithRotation_ShouldBeThreadSafe()
    {
        // Arrange
        const int capacity = 100;
        const int threadCount = 10;
        const int itemsPerThread = 100; // Total 1000 items, capacity 100
        var buffer = new OutputLogBuffer(capacity);
        var tasks = new List<Task>();

        // Act
        for (var i = 0; i < threadCount; i++)
        {
            tasks.Add(Task.Run(
                () =>
                {
                    for (var j = 0; j < itemsPerThread; j++)
                    {
                        buffer.Append(new OutputLogEntry
                        {
                            Message = string.Create(CultureInfo.InvariantCulture, $"Message {j}"),
                        });
                    }
                },
                this.TestContext.CancellationToken));
        }

        Task.WaitAll([.. tasks]);

        // Assert
        _ = buffer.Count.Should().Be(capacity);
    }

    [TestMethod]
    public void BeginUpdate_ShouldSuppressNotifications()
    {
        // Arrange
        var buffer = new OutputLogBuffer(10);
        var eventCount = 0;
        buffer.CollectionChanged += (_, _) => eventCount++;

        // Act
        buffer.BeginUpdate();
        buffer.Append(new OutputLogEntry { Message = "1" });
        buffer.Append(new OutputLogEntry { Message = "2" });
        buffer.Append(new OutputLogEntry { Message = "3" });
        buffer.EndUpdate();

        // Assert
        _ = buffer.Count.Should().Be(3);
        _ = eventCount.Should().Be(1); // Only one Reset event at EndUpdate
    }

    [TestMethod]
    public void BeginUpdate_Nested_ShouldOnlyNotifyOnOutermostEnd()
    {
        // Arrange
        var buffer = new OutputLogBuffer(10);
        var eventCount = 0;
        buffer.CollectionChanged += (_, _) => eventCount++;

        // Act
        buffer.BeginUpdate();
        buffer.Append(new OutputLogEntry { Message = "1" });
        buffer.BeginUpdate(); // Nested
        buffer.Append(new OutputLogEntry { Message = "2" });
        buffer.EndUpdate(); // End inner
        buffer.Append(new OutputLogEntry { Message = "3" });
        buffer.EndUpdate(); // End outer

        // Assert
        _ = buffer.Count.Should().Be(3);
        _ = eventCount.Should().Be(1); // Only one Reset event when depth reaches 0
    }

    [TestMethod]
    public void Append_ShouldRaiseAddAndPropertyChangedEvents()
    {
        // Arrange
        var buffer = new OutputLogBuffer(3);
        var propertyChanges = new List<string>();
        NotifyCollectionChangedEventArgs? lastArgs = null;

        buffer.CollectionChanged += (_, e) => lastArgs = e;
        buffer.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is not null)
            {
                propertyChanges.Add(e.PropertyName);
            }
        };

        var entry = new OutputLogEntry { Message = "Test Add" };

        // Act
        buffer.Append(entry);

        // Assert
        _ = lastArgs.Should().NotBeNull();
        _ = lastArgs!.Action.Should().Be(NotifyCollectionChangedAction.Add);
        _ = lastArgs.NewItems![0].Should().Be(entry);
        _ = propertyChanges.Should().Contain(nameof(buffer.Count));
        _ = propertyChanges.Should().Contain("Item[]");
    }

    [TestMethod]
    public void Append_WhenRotation_ShouldRaiseResetAndPropertyChanged()
    {
        // Arrange
        var buffer = new OutputLogBuffer(2);
        buffer.Append(new OutputLogEntry { Message = "1" });
        buffer.Append(new OutputLogEntry { Message = "2" });

        NotifyCollectionChangedEventArgs? args = null;
        var props = new List<string>();
        buffer.CollectionChanged += (_, e) => args = e;
        buffer.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is not null)
            {
                props.Add(e.PropertyName);
            }
        };

        // Act
        buffer.Append(new OutputLogEntry { Message = "3" });

        // Assert
        _ = args.Should().NotBeNull();
        _ = args!.Action.Should().Be(NotifyCollectionChangedAction.Reset);
        _ = props.Should().Contain(nameof(buffer.Count));
        _ = props.Should().Contain("Item[]");
    }

    [TestMethod]
    public void GetEnumerator_ShouldReturnSnapshot_WhenBufferIsModifiedAfterEnumerationBegins()
    {
        // Arrange
        var buffer = new OutputLogBuffer(100);
        for (var i = 0; i < 10; i++)
        {
            buffer.Append(new OutputLogEntry
            {
                Message = i.ToString(System.Globalization.CultureInfo.InvariantCulture),
            });
        }

        // Act
        var snapshot = buffer.ToList();

        // mutate after taking snapshot
        for (var i = 10; i < 20; i++)
        {
            buffer.Append(new OutputLogEntry
            {
                Message = i.ToString(System.Globalization.CultureInfo.InvariantCulture),
            });
        }

        // Assert
        _ = snapshot.Should().HaveCount(10);
        _ = buffer.Should().HaveCount(20);
    }

    [TestMethod]
    public void BeginUpdate_WithoutChanges_ShouldNotRaiseNotifications()
    {
        // Arrange
        var buffer = new OutputLogBuffer(10);
        var events = 0;
        buffer.CollectionChanged += (_, _) => events++;

        // Act
        buffer.BeginUpdate();

        // don't append anything
        buffer.EndUpdate();

        // Assert
        _ = events.Should().Be(0);
    }

    [TestMethod]
    [TestCategory("Stress")]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0051:Method is too long", Justification = "keep test method in a single block")]
    public void AddEvent_HandlerReadsByIndex_Stress_NoRotation_ShouldNotSeeMismatches()
    {
        // Arrange
        const int threadCount = 4;
        const int itemsPerThread = 500; // total 2k
        const int capacity = threadCount * itemsPerThread * 2; // ensure no rotation

        var buffer = new OutputLogBuffer(capacity);

        var mismatchCount = 0;
        var errorCount = 0;

        buffer.CollectionChanged += (_, e) =>
        {
            if (e.Action != NotifyCollectionChangedAction.Add)
            {
                return;
            }

            try
            {
                var idx = e.NewStartingIndex;
                var newItems = e.NewItems;
                if (newItems is null || newItems.Count == 0)
                {
                    _ = Interlocked.Increment(ref errorCount);
                    return;
                }

                if (newItems[0] is not OutputLogEntry evItem)
                {
                    _ = Interlocked.Increment(ref errorCount);
                    return;
                }

                // small delay to expose races
                Thread.Sleep(1);

                if (idx < 0 || idx >= buffer.Count)
                {
                    _ = Interlocked.Increment(ref errorCount);
                }
                else
                {
                    if (!ReferenceEquals(buffer[idx], evItem))
                    {
                        _ = Interlocked.Increment(ref mismatchCount);
                    }
                }
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch
            {
                _ = Interlocked.Increment(ref errorCount);
            }
#pragma warning restore CA1031 // Do not catch general exception types
        };

        // Act
        var tasks = new List<Task>();
        for (var t = 0; t < threadCount; t++)
        {
            tasks.Add(Task.Run(
                () =>
                {
                    for (var i = 0; i < itemsPerThread; i++)
                    {
                        buffer.Append(new OutputLogEntry { Message = Guid.NewGuid().ToString("N") });
                    }
                },
                this.TestContext.CancellationToken));
        }

        Task.WaitAll([.. tasks]);

        // Assert
        _ = mismatchCount.Should().Be(0, string.Create(CultureInfo.InvariantCulture, $"expected zero mismatches but found {mismatchCount}"));
        _ = errorCount.Should().Be(0, string.Create(CultureInfo.InvariantCulture, $"expected zero errors but found {errorCount}"));
    }

    [TestMethod]
    [TestCategory("Stress")]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0051:Method is too long", Justification = "keep test method in a single block")]
    public void AddEvent_HandlerReadsByIndex_Stress_WithRotation_ShouldNotThrowOrMismatch()
    {
        // Arrange
        const int threadCount = 4;
        const int itemsPerThread = 1000; // total 4k
        const int capacity = 100; // will rotate frequently

        var buffer = new OutputLogBuffer(capacity);

        var mismatchCount = 0;
        var errorCount = 0;

        buffer.CollectionChanged += (_, e) =>
        {
            if (e.Action != NotifyCollectionChangedAction.Add)
            {
                return;
            }

#pragma warning disable CA1031 // Do not catch general exception types
            try
            {
                var idx = e.NewStartingIndex;
                var newItems = e.NewItems;
                if (newItems is null || newItems.Count == 0)
                {
                    _ = Interlocked.Increment(ref errorCount);
                    return;
                }

                if (newItems[0] is not OutputLogEntry evItem)
                {
                    _ = Interlocked.Increment(ref errorCount);
                    return;
                }

                Thread.Sleep(1);

                // if rotation removed items and index is out of range we consider it an error
                if (idx < 0 || idx >= buffer.Count)
                {
                    _ = Interlocked.Increment(ref errorCount);
                }
                else if (!ReferenceEquals(buffer[idx], evItem))
                {
                    _ = Interlocked.Increment(ref mismatchCount);
                }
            }
            catch
            {
                _ = Interlocked.Increment(ref errorCount);
            }
#pragma warning restore CA1031 // Do not catch general exception types
        };

        // Act
        var tasks = new List<Task>();
        for (var t = 0; t < threadCount; t++)
        {
            tasks.Add(Task.Run(
                () =>
                {
                    for (var i = 0; i < itemsPerThread; i++)
                    {
                        buffer.Append(new OutputLogEntry { Message = Guid.NewGuid().ToString("N") });
                    }
                },
                this.TestContext.CancellationToken));
        }

        Task.WaitAll([.. tasks]);

        // Assert
        // During rotation it's valid for indices to be shifted before the handler reads them
        // (the buffer can remove older items), but the handler should never observe exceptions
        // or index-out-of-range — those are considered failures.
        _ = errorCount.Should().Be(0, string.Create(CultureInfo.InvariantCulture, $"expected zero errors but found {errorCount}"));
    }

    [TestMethod]
    public void Add_EventThenPropertyChanged_Order_IsCollectionThenCountThenIndexer()
    {
        // Arrange
        var buffer = new OutputLogBuffer(5);
        var seq = new List<string>();

        buffer.CollectionChanged += (_, e) => seq.Add("C:" + e.Action);
        buffer.PropertyChanged += (_, e) => seq.Add("P:" + (e.PropertyName ?? "<null>"));

        // Act
        buffer.Append(new OutputLogEntry { Message = "a" });

        // Assert
        _ = seq.Should().HaveCount(3);
        _ = seq[0].Should().StartWith("C:Add");
        _ = seq[1].Should().Be($"P:{nameof(buffer.Count)}");
        _ = seq[2].Should().Be("P:Item[]");
    }

    [TestMethod]
    public void Rotation_EventThenPropertyChanged_Order_IsResetThenCountThenIndexer()
    {
        // Arrange
        var buffer = new OutputLogBuffer(2);
        var seq = new List<string>();
        buffer.CollectionChanged += (_, e) => seq.Add("C:" + e.Action);
        buffer.PropertyChanged += (_, e) => seq.Add("P:" + (e.PropertyName ?? "<null>"));

        buffer.Append(new OutputLogEntry { Message = "1" });
        buffer.Append(new OutputLogEntry { Message = "2" });

        // Act - cause rotation
        buffer.Append(new OutputLogEntry { Message = "3" });

        // Assert
        _ = seq.Should().ContainInOrder("C:Reset", $"P:{nameof(buffer.Count)}", "P:Item[]");
    }

    [TestMethod]
    public void BeginUpdate_EndUpdate_EventOrdering_IsResetThenPropertyChanges()
    {
        // Arrange
        var buffer = new OutputLogBuffer(10);
        var seq = new List<string>();
        buffer.CollectionChanged += (_, e) => seq.Add("C:" + e.Action);
        buffer.PropertyChanged += (_, e) => seq.Add("P:" + (e.PropertyName ?? "<null>"));

        // Act
        buffer.BeginUpdate();
        buffer.Append(new OutputLogEntry { Message = "1" });
        buffer.Append(new OutputLogEntry { Message = "2" });
        buffer.EndUpdate();

        // Assert
        _ = seq.Should().ContainInOrder("C:Reset", $"P:{nameof(buffer.Count)}", "P:Item[]");
    }

    [TestMethod]
    public void Pause_Unpause_RaisesReset_Only()
    {
        // Arrange
        var buffer = new OutputLogBuffer(10);
        var seq = new List<string>();
        buffer.CollectionChanged += (_, e) => seq.Add("C:" + e.Action);
        buffer.PropertyChanged += (_, e) => seq.Add("P:" + (e.PropertyName ?? "<null>"));

        buffer.IsPaused = true;
        buffer.Append(new OutputLogEntry { Message = "x" });

        // Act
        buffer.IsPaused = false; // unpause -> Reset expected

        // Assert
        _ = seq.Should().ContainSingle().Which.Should().StartWith("C:Reset");
        _ = seq.Should().NotContain(s => s.StartsWith("P:"));
    }
}
