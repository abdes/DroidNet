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
/// Unit test cases for the "Undo" functionality of the <see cref="HistoryKeeper" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("HistoryKeeper")]
public partial class HistoryKeeperTests
{
    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldMoveChangesToRedoStack()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];

        // Act
        Action();
        historyKeeper.Undo();

        // Assert
        _ = historyKeeper.RedoStack.Should().ContainSingle();

        void Action()
        {
            _ = 1;
            historyKeeper.AddChange("undo = reset", ReverseAction);
        }

        void ReverseAction()
        {
            _ = 0;
            historyKeeper.AddChange("redo = set", Action);
        }
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldApplyChanges()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        var change = new Mock<IChange>();
        historyKeeper.AddChange(change.Object);

        // Act
        historyKeeper.Undo();

        // Assert
        change.Verify(cs => cs.Apply(), Times.Once);
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldCommitTransactions_WhenTransactionsExist()
    {
        // Arrange
        var mockTransactionFactory = new Mock<ITransactionFactory>();
        var mockTransaction = new Mock<ITransaction>();
        _ = mockTransactionFactory.Setup(tf => tf.CreateTransaction(It.IsAny<object>())).Returns(mockTransaction.Object);

        var historyKeeper = new HistoryKeeper(new object(), mockTransactionFactory.Object);
        historyKeeper.AddChange(new Mock<IChange>().Object);

        // Simulate an existing transaction
        var transaction = historyKeeper.BeginTransaction("t");
        transaction.AddChange(new Mock<IChange>().Object);

        // Act
        historyKeeper.Undo();

        // Assert
        mockTransaction.Verify(t => t.Commit(), Times.Once);
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldNotThrow_WhenUndoStackIsEmpty()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];

        // Act
        var act = historyKeeper.Undo;

        // Assert
        _ = act.Should().NotThrow();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldNotChangeState()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        historyKeeper.AddChange(new Mock<IChange>().Object);

        const HistoryKeeper.States initialState = HistoryKeeper.States.Redoing; // Set an initial state
        historyKeeper.State = initialState;

        // Act
        historyKeeper.Undo();

        // Assert
        _ = historyKeeper.State.Should().Be(initialState);
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldNotChangeState_WhenApplyThrowsException()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        var change = new Mock<IChange>();
        _ = change.Setup(cs => cs.Apply()).Throws<Exception>();
        historyKeeper.AddChange(change.Object);

        const HistoryKeeper.States initialState = HistoryKeeper.States.Redoing; // Set an initial state
        historyKeeper.State = initialState;

        // Act
        var undone = historyKeeper.Undo();

        // Assert
        _ = undone.Should().BeFalse();
        _ = historyKeeper.State.Should().Be(initialState);
        change.Verify(cs => cs.Apply(), Times.Once);
        _ = historyKeeper.RedoStack.Should().BeEmpty();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldRestoreChangeToUndoStack_WhenApplyThrowsException()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        var change = new Mock<IChange>();
        _ = change.Setup(cs => cs.Apply()).Throws<Exception>();
        historyKeeper.AddChange(change.Object);
        var position = historyKeeper.Position;

        // Act
        var undone = historyKeeper.Undo();

        // Assert
        _ = undone.Should().BeFalse();
        _ = historyKeeper.UndoStack.Should().ContainSingle().Which.Should().BeSameAs(change.Object);
        _ = historyKeeper.RedoStack.Should().BeEmpty();
        _ = historyKeeper.Position.Should().Be(position);
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldReturnFalse_WhenUndoStackIsEmpty()
        => _ = UndoRedo.Default[new object()].Undo().Should().BeFalse();

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void UndoAndRedo_ShouldReturnTrue_WhenTheChangeApplies()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        historyKeeper.AddChange("undo", () => historyKeeper.AddChange("redo", static () => { }));

        // Act and assert
        _ = historyKeeper.Undo().Should().BeTrue();
        _ = historyKeeper.Redo().Should().BeTrue();
        _ = historyKeeper.Redo().Should().BeFalse();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Redo_FailureRetainsStepAtItsPositionForRetry()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        var fail = true;
        historyKeeper.AddChange("undo", () => historyKeeper.AddChange("redo", () =>
        {
            if (fail)
            {
                throw new InvalidOperationException("Controlled redo failure");
            }

            historyKeeper.AddChange("undo again", static () => { });
        }));
        _ = historyKeeper.Undo().Should().BeTrue();
        var redo = historyKeeper.RedoStack.Single();
        var position = historyKeeper.Position;

        // Act
        var failed = historyKeeper.Redo();

        // Assert
        _ = failed.Should().BeFalse();
        _ = historyKeeper.State.Should().Be(HistoryKeeper.States.Idle);
        _ = historyKeeper.RedoStack.Should().ContainSingle().Which.Should().BeSameAs(redo);
        _ = historyKeeper.UndoStack.Should().BeEmpty();
        _ = historyKeeper.Position.Should().Be(position);
        fail = false;
        _ = historyKeeper.Redo().Should().BeTrue();
        _ = historyKeeper.RedoStack.Should().BeEmpty();
        _ = historyKeeper.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public void Redo_ShouldCommitTransactions_WhenTransactionsExist()
    {
        // Arrange
        var mockTransactionFactory = new Mock<ITransactionFactory>();
        var mockTransaction = new Mock<ITransaction>();
        _ = mockTransactionFactory.Setup(tf => tf.CreateTransaction(It.IsAny<object>())).Returns(mockTransaction.Object);

        var historyKeeper = new HistoryKeeper(new object(), mockTransactionFactory.Object);
        historyKeeper.AddChange(new Mock<IChange>().Object);
        historyKeeper.Undo();

        // Simulate an existing transaction
        var transaction = historyKeeper.BeginTransaction("t");
        transaction.AddChange(new Mock<IChange>().Object);

        // Act
        historyKeeper.Redo();

        // Assert
        mockTransaction.Verify(t => t.Commit(), Times.Once);
    }

    [TestMethod]
    public void Redo_ShouldNotThrow_WhenRedoStackIsEmpty()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];

        // Act
        var act = historyKeeper.Redo;

        // Assert
        _ = act.Should().NotThrow();
    }
}
