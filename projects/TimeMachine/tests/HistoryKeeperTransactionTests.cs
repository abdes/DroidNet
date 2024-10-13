// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Tests;

using System.Diagnostics.CodeAnalysis;
using DroidNet.TimeMachine.Transactions;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using static DroidNet.TimeMachine.HistoryKeeper;

[TestClass]
[ExcludeFromCodeCoverage]
public class HistoryKeeperTransactionTests
{
    private readonly Mock<ITransactionFactory> transactionFactoryMock;
    private readonly Mock<ITransaction> transactionMock;
    private readonly HistoryKeeper historyKeeper;

    public HistoryKeeperTransactionTests()
    {
        this.transactionFactoryMock = new Mock<ITransactionFactory>();
        this.transactionMock = new Mock<ITransaction>();
        this.historyKeeper = new HistoryKeeper(new object(), this.transactionFactoryMock.Object);
    }

    [TestMethod]
    public void CommitTransaction_ShouldAddTransactionToUndoStack_WhenStateIsIdle()
    {
        // Arrange
        this.transactionFactoryMock.Setup(f => f.CreateTransaction(It.IsAny<object>()))
            .Returns(this.transactionMock.Object);
        var transaction = this.historyKeeper.BeginTransaction("t");

        // Act
        this.historyKeeper.CommitTransaction(transaction);

        // Assert
        this.historyKeeper.UndoStack.Should().Contain(transaction);
    }

    [TestMethod]
    public void CommitTransaction_ShouldAddTransactionToRedoStack_WhenStateIsUndoing()
    {
        // Arrange
        this.historyKeeper.State = States.Undoing;
        this.transactionFactoryMock.Setup(f => f.CreateTransaction(It.IsAny<object>()))
            .Returns(this.transactionMock.Object);
        var transaction = this.historyKeeper.BeginTransaction("t");

        // Act
        this.historyKeeper.CommitTransaction(transaction);

        // Assert
        this.historyKeeper.RedoStack.Should().Contain(transaction);
    }

    [TestMethod]
    public void CommitTransaction_ShouldHandleNestedTransactionsCorrectly()
    {
        // Arrange
        var nestedTransactionMock = new Mock<ITransaction>();
        var parentTransactionMock = new Mock<ITransaction>();
        this.transactionFactoryMock.SetupSequence(f => f.CreateTransaction(It.IsAny<object>()))
            .Returns(parentTransactionMock.Object)
            .Returns(nestedTransactionMock.Object);

        var parentTransaction = this.historyKeeper.BeginTransaction(new object());
        var nestedTransaction = this.historyKeeper.BeginTransaction(new object());

        // Act
        this.historyKeeper.CommitTransaction(parentTransaction);

        // Assert
        this.historyKeeper.UndoStack.Should().Contain(parentTransaction);
        this.historyKeeper.UndoStack.Should().NotContain(nestedTransaction);
        parentTransactionMock.Verify(t => t.AddChange(nestedTransaction), Times.Once);
    }

    [TestMethod]
    public void RollbackTransaction_ShouldCallRollbackOnNestedTransactions()
    {
        // Arrange
        var parentTransactionMock = new Mock<ITransaction>();
        var nestedTransactionMock = new Mock<ITransaction>();
        this.transactionFactoryMock.SetupSequence(f => f.CreateTransaction(It.IsAny<object>()))
            .Returns(parentTransactionMock.Object)
            .Returns(nestedTransactionMock.Object);

        var parentTransaction = this.historyKeeper.BeginTransaction(new object());
        _ = this.historyKeeper.BeginTransaction(new object());

        // Act
        this.historyKeeper.RollbackTransaction(parentTransaction);

        // Assert
        nestedTransactionMock.Verify(t => t.Rollback(), Times.Once);
        parentTransactionMock.Verify(t => t.Rollback(), Times.Never);
    }
}
