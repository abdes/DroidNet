// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Tests.Transactions;

using System.Diagnostics.CodeAnalysis;
using DroidNet.TimeMachine.Changes;
using DroidNet.TimeMachine.Transactions;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Changes")]
public class TransactionTests
{
    private readonly Mock<ITransactionManager> mockTransactionManager = new();
    private readonly Mock<IChange> mockChange = new();
    private readonly object key = new();

    [TestMethod]
    public void Dispose_ShouldCommitTransaction_WhenCalled()
    {
        // Arrange
        var transaction = new Transaction(this.mockTransactionManager.Object, this.key);

        // Act
        transaction.Dispose();

        // Assert
        this.mockTransactionManager.Verify(m => m.CommitTransaction(transaction), Times.Once);
    }

    [TestMethod]
    public void Dispose_ShouldNotCommitTransaction_WhenAlreadyDisposed()
    {
        // Arrange
        var transaction = new Transaction(this.mockTransactionManager.Object, this.key);
        transaction.Dispose();

        // Act
        transaction.Dispose();

        // Assert
        this.mockTransactionManager.Verify(m => m.CommitTransaction(transaction), Times.Once);
    }

    [TestMethod]
    public void Commit_ShouldCallCommitTransaction_OnOwner()
    {
        // Arrange
        using var transaction = new Transaction(this.mockTransactionManager.Object, this.key);

        // Act
        transaction.Commit();

        // Assert
        this.mockTransactionManager.Verify(m => m.CommitTransaction(transaction), Times.Once);
    }

    [TestMethod]
    public void Rollback_ShouldCallRollbackTransaction_OnOwner()
    {
        // Arrange
        using var transaction = new Transaction(this.mockTransactionManager.Object, this.key);

        // Act
        transaction.Rollback();

        // Assert
        this.mockTransactionManager.Verify(m => m.RollbackTransaction(transaction), Times.Once);
    }

    [TestMethod]
    public void AddChange_ShouldAddChange_ToChangeSet()
    {
        // Arrange
        using var transaction = new Transaction(this.mockTransactionManager.Object, this.key);

        // Act
        transaction.AddChange(this.mockChange.Object);

        // Assert
        transaction.Changes.Should().Contain(this.mockChange.Object);
    }

    [TestMethod]
    public void NestedTransactions_ShouldCommitOuterTransaction_WhenDisposed()
    {
        // Arrange
        var outerTransaction = new Transaction(this.mockTransactionManager.Object, this.key);
        var innerTransaction = new Transaction(this.mockTransactionManager.Object, this.key);

        // Act
        innerTransaction.Dispose();
        outerTransaction.Dispose();

        // Assert
        this.mockTransactionManager.Verify(m => m.CommitTransaction(outerTransaction), Times.Once);
    }

    [TestMethod]
    public void Apply_ShouldCallApplyOnAllChanges()
    {
        // Arrange
        var changeMock1 = new Mock<IChange>();
        var changeMock2 = new Mock<IChange>();
        using var transaction = new Transaction(this.mockTransactionManager.Object, this.key);

        transaction.AddChange(changeMock1.Object);
        transaction.AddChange(changeMock2.Object);

        // Act
        transaction.Apply();

        // Assert
        changeMock1.Verify(c => c.Apply(), Times.Once);
        changeMock2.Verify(c => c.Apply(), Times.Once);
    }

    [TestMethod]
    public void Constructor_ShouldInitializeKey()
    {
        // Arrange

        // Act
        using var transaction = new Transaction(this.mockTransactionManager.Object, this.key);

        // Assert
        transaction.Key.Should().Be(this.key);
    }
}
