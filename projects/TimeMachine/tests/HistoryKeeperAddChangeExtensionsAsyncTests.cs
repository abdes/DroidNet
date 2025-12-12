// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Linq.Expressions;
using DroidNet.TimeMachine.Changes;
using DroidNet.TimeMachine.Transactions;
using Moq;

namespace DroidNet.TimeMachine.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(HistoryKeeper)}.AddChangeExtensions.Async")]
public class HistoryKeeperAddChangeExtensionsAsyncTests
{
    private readonly Mock<HistoryKeeper> historyKeeperMock = new(new object(), new Mock<ITransactionFactory>().Object);

    public TestContext TestContext { get; set; }

    [TestMethod]
    public void AddChange_WithAsyncLambdaExpressionValueTask_ShouldAddAsyncChangeToUndoManager()
    {
        // Arrange
        var targetMock = new Mock<IAsyncTestTarget>();
        Expression<Func<IAsyncTestTarget, ValueTask>> expression = t => t.TestValueTaskAsync();
        const string label = "TestLabel";

        // Act
        this.historyKeeperMock.Object.AddChange(label, targetMock.Object, expression);

        // Assert
        this.historyKeeperMock.Verify(
            h => h.AddChange(It.Is<AsyncLambdaExpressionOnTarget<IAsyncTestTarget>>(a => (string)a.Key == label)),
            Times.Once);
    }

    [TestMethod]
    public void AddChange_WithAsyncLambdaExpressionTask_ShouldAddAsyncChangeToUndoManager()
    {
        // Arrange
        var targetMock = new Mock<IAsyncTestTarget>();
        Expression<Func<IAsyncTestTarget, Task>> expression = t => t.TestTaskAsync(this.TestContext.CancellationToken);
        const string label = "TestLabel";

        // Act
        this.historyKeeperMock.Object.AddChange(label, targetMock.Object, expression);

        // Assert
        this.historyKeeperMock.Verify(
            h => h.AddChange(It.Is<AsyncLambdaExpressionOnTarget<IAsyncTestTarget>>(a => (string)a.Key == label)),
            Times.Once);
    }

    [TestMethod]
    public void AddChange_WithAsyncLambdaOnTargetAndCancellation_ShouldAddAsyncChangeToUndoManager()
    {
        // Arrange
        var targetMock = new Mock<IAsyncTestTarget>();
        const string label = "TestLabel";

        // Act
        this.historyKeeperMock.Object.AddChange(label, targetMock.Object, (t, ct) => t.TestTaskAsync(ct));

        // Assert
        this.historyKeeperMock.Verify(
            h => h.AddChange(It.Is<AsyncLambdaExpressionOnTarget<IAsyncTestTarget>>(a => (string)a.Key == label)),
            Times.Once);
    }

    [TestMethod]
    public void AddChange_WithAsyncActionAndArgument_ShouldAddAsyncChangeToUndoManager()
    {
        // Arrange
        static Task Action(string? arg) => Task.CompletedTask;
        const string argument = "TestArgument";
        const string label = "TestLabel";

        // Act
        this.historyKeeperMock.Object.AddChange(label, Action, argument);

        // Assert
        this.historyKeeperMock.Verify(
            h => h.AddChange(It.Is<AsyncActionWithArgument<string?>>(a => (string)a.Key == label)),
            Times.Once);
    }

    [TestMethod]
    public void AddChange_WithAsyncActionAndArgumentAndCancellation_ShouldAddAsyncChangeToUndoManager()
    {
        // Arrange
        static Task Action(string? arg, CancellationToken ctk) => Task.CompletedTask;
        const string argument = "TestArgument";
        const string label = "TestLabel";

        // Act
        this.historyKeeperMock.Object.AddChange(label, Action, argument);

        // Assert
        this.historyKeeperMock.Verify(
            h => h.AddChange(It.Is<AsyncActionWithArgument<string?>>(a => (string)a.Key == label)),
            Times.Once);
    }

    [TestMethod]
    public void AddChange_WithAsyncSimpleAction_ShouldAddAsyncChangeToUndoManager()
    {
        // Arrange
        const string label = "TestLabel";

        // Act
        this.historyKeeperMock.Object.AddChange(label, () => Task.CompletedTask);

        // Assert
        this.historyKeeperMock.Verify(
            h => h.AddChange(It.Is<AsyncSimpleAction>(a => (string)a.Key == label)),
            Times.Once);
    }
}
