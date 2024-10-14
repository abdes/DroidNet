// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Tests;

using System.Diagnostics.CodeAnalysis;
using DroidNet.TimeMachine.Changes;
using DroidNet.TimeMachine.Transactions;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

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
        historyKeeper.RedoStack.Should().ContainSingle();

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
        mockTransactionFactory.Setup(tf => tf.CreateTransaction(It.IsAny<object>())).Returns(mockTransaction.Object);

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
        act.Should().NotThrow();
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
        historyKeeper.State.Should().Be(initialState);
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldNotChangeState_WhenApplyThrowsException()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        var change = new Mock<IChange>();
        change.Setup(cs => cs.Apply()).Throws<Exception>();
        historyKeeper.AddChange(change.Object);

        const HistoryKeeper.States initialState = HistoryKeeper.States.Redoing; // Set an initial state
        historyKeeper.State = initialState;

        // Act
        var act = historyKeeper.Undo;

        // Assert
        act.Should().Throw<Exception>();
        historyKeeper.State.Should().Be(initialState);
        change.Verify(cs => cs.Apply(), Times.Once);
        historyKeeper.RedoStack.Should().BeEmpty();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldNotMoveChangeToRedoStack_WhenApplyThrowsException()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        var change = new Mock<IChange>();
        change.Setup(cs => cs.Apply()).Throws<Exception>();
        historyKeeper.AddChange(change.Object);

        // Act
        var act = historyKeeper.Undo;

        // Assert
        act.Should().Throw<Exception>();
        historyKeeper.RedoStack.Should().BeEmpty();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Undo")]
    public void Undo_ShouldRaiseEvents_WhenUndoIsPerformed()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        using var monitoredSubject = historyKeeper.Monitor();

        // Act
        Action();
        historyKeeper.Undo();

        // Assert
        monitoredSubject.Should().Raise("UndoStackChanged");
        monitoredSubject.Should().Raise("RedoStackChanged");

        void ReverseAction()
        {
            _ = 0;
            historyKeeper.AddChange("redo = set", Action);
        }

        void Action()
        {
            _ = 1;
            historyKeeper.AddChange("undo = reset", ReverseAction);
        }
    }

    [TestMethod]
    public void Redo_ShouldCommitTransactions_WhenTransactionsExist()
    {
        // Arrange
        var mockTransactionFactory = new Mock<ITransactionFactory>();
        var mockTransaction = new Mock<ITransaction>();
        mockTransactionFactory.Setup(tf => tf.CreateTransaction(It.IsAny<object>())).Returns(mockTransaction.Object);

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
        act.Should().NotThrow();
    }

    [TestMethod]
    public void Redo_ShouldRaiseEvents_WhenRedoIsPerformed()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];

        Action();

        historyKeeper.Undo();
        historyKeeper.CanRedo.Should().BeTrue();

        using var monitoredSubject = historyKeeper.Monitor();

        // Act
        historyKeeper.Redo();

        // Assert
        monitoredSubject.Should().Raise("RedoStackChanged");
        monitoredSubject.Should().Raise("UndoStackChanged");

        void ReverseAction()
        {
            _ = 0;
            historyKeeper.AddChange("redo = set", Action);
        }

        void Action()
        {
            _ = 1;
            historyKeeper.AddChange("undo = reset", ReverseAction);
        }
    }
}
