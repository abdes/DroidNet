// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.TimeMachine.Changes;
using DroidNet.TimeMachine.Transactions;
using Moq;

namespace DroidNet.TimeMachine.Tests;

/// <summary>
/// Unit test cases for the async Undo/Redo functionality of the <see cref="HistoryKeeper"/> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("HistoryKeeper.Async")]
public class HistoryKeeperAsyncTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    [TestCategory("HistoryKeeper.UndoAsync")]
    public async Task UndoAsync_ShouldApplyChangesAsync()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        var change = new Mock<IChange>();
        _ = change.Setup(c => c.ApplyAsync(It.IsAny<CancellationToken>())).Returns(ValueTask.CompletedTask);
        _ = change.Setup(c => c.Apply()).Throws<InvalidOperationException>();
        historyKeeper.AddChange(change.Object);

        // Act
        await historyKeeper.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        change.Verify(c => c.ApplyAsync(It.IsAny<CancellationToken>()), Times.Once);
        change.Verify(c => c.Apply(), Times.Never);
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.UndoAsync")]
    public async Task UndoAsync_ShouldMoveChangesToRedoStack_WhenChangeAddsRedoAction()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];

        // Act
        DoAction();
        await historyKeeper.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = historyKeeper.RedoStack.Should().ContainSingle();

        void DoAction()
        {
            historyKeeper.AddChange("undo", UndoAsync);
        }

        async Task UndoAsync()
        {
            await Task.Yield();
            historyKeeper.AddChange("redo", DoAction);
        }
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldThrow_ForAsyncChange()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        static async Task Undo()
        {
            await Task.Yield();
        }

        historyKeeper.AddChange("undo", Undo);

        // Act
        var act = historyKeeper.Undo;

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.RedoAsync")]
    public async Task RedoAsync_ShouldApplyChangesAsync()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        static Task UndoAgain() => Task.CompletedTask;

        async Task Redo()
        {
            await Task.Yield();
            historyKeeper.AddChange("undo-again", UndoAgain);
        }

        async Task Undo()
        {
            await Task.Yield();
            historyKeeper.AddChange("redo", Redo);
        }

        historyKeeper.AddChange("undo", Undo);

        await historyKeeper.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Act
        await historyKeeper.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = historyKeeper.UndoStack.Should().NotBeEmpty();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.UndoAsync")]
    public async Task UndoAsync_ShouldCommitTransactions_WhenTransactionsExist()
    {
        // Arrange
        var mockTransactionFactory = new Mock<ITransactionFactory>();
        var mockTransaction = new Mock<ITransaction>();
        _ = mockTransactionFactory.Setup(tf => tf.CreateTransaction(It.IsAny<object>())).Returns(mockTransaction.Object);

        var historyKeeper = new HistoryKeeper(new object(), mockTransactionFactory.Object);
        historyKeeper.AddChange("undo", () => Task.CompletedTask);

        var transaction = historyKeeper.BeginTransaction("t");
        transaction.AddChange(new Mock<IChange>().Object);

        // Act
        await historyKeeper.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        mockTransaction.Verify(t => t.Commit(), Times.Once);
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.UndoAsync")]
    public async Task UndoAsync_ShouldNotThrow_WhenUndoStackIsEmpty()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];

        // Act
        Func<Task> act = async () => await historyKeeper.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = await act.Should().NotThrowAsync().ConfigureAwait(false);
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.RedoAsync")]
    public async Task RedoAsync_ShouldNotThrow_WhenRedoStackIsEmpty()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];

        // Act
        Func<Task> act = async () => await historyKeeper.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = await act.Should().NotThrowAsync().ConfigureAwait(false);
    }
}
