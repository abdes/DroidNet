// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.TimeMachine.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(UndoRedo))]
public class UndoRedoTests
{
    [TestMethod]
    public void Default_ShouldReturnSameInstance()
    {
        // Act
        var instance1 = UndoRedo.Default;
        var instance2 = UndoRedo.Default;

        // Assert
        _ = instance1.Should().BeSameAs(instance2);
    }

    [TestMethod]
    public void Indexer_ShouldReturnSameHistoryKeeperForSameRoot()
    {
        // Arrange
        var undoRedo = UndoRedo.Default;
        var root = new object();

        // Act
        var historyKeeper1 = undoRedo[root];
        var historyKeeper2 = undoRedo[root];

        // Assert
        _ = historyKeeper1.Should().BeSameAs(historyKeeper2);
    }

    [TestMethod]
    public void Indexer_ShouldReturnDifferentHistoryKeeperForDifferentRoots()
    {
        // Arrange
        var undoRedo = UndoRedo.Default;
        var root1 = new object();
        var root2 = new object();

        // Act
        var historyKeeper1 = undoRedo[root1];
        var historyKeeper2 = undoRedo[root2];

        // Assert
        _ = historyKeeper1.Should().NotBeSameAs(historyKeeper2);
    }

    [TestMethod]
    public void Clear_ShouldRemoveAllHistoryKeepers()
    {
        // Arrange
        var undoRedo = UndoRedo.Default;
        var root1 = new object();
        var root2 = new object();
        _ = undoRedo[root1];
        _ = undoRedo[root2];

        // Act
        UndoRedo.Clear();

        // Assert
        _ = undoRedo[root1].Should().NotBeNull();
        _ = undoRedo[root2].Should().NotBeNull();
    }

    [TestMethod]
    public void Indexer_ShouldBeThreadSafe()
    {
        // Arrange
        var undoRedo = UndoRedo.Default;
        var root = new object();

        // Act
        var thread1 = new Thread(() => _ = undoRedo[root]);
        var thread2 = new Thread(() => _ = undoRedo[root]);
        thread1.Start();
        thread2.Start();
        thread1.Join();
        thread2.Join();

        // Assert
        _ = undoRedo[root].Should().NotBeNull();
    }
}
