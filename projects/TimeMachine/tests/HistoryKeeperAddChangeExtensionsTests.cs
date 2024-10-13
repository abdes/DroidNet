// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Tests;

using System.Diagnostics.CodeAnalysis;
using System.Linq.Expressions;
using DroidNet.TimeMachine.Changes;
using DroidNet.TimeMachine.Transactions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(HistoryKeeper)}.AddChangeExtensions")]
public class HistoryKeeperAddChangeExtensionsTests
{
    private readonly Mock<HistoryKeeper> historyKeeperMock = new(new object(), new Mock<ITransactionFactory>().Object);

    [SuppressMessage("ReSharper", "MemberCanBePrivate.Global", Justification = "visible for mocking")]
    public interface ITestTarget
    {
        void TestMethod();
    }

    [TestMethod]
    public void AddChange_WithLambdaExpression_ShouldAddChangeToUndoManager()
    {
        // Arrange
        var targetMock = new Mock<ITestTarget>();
        Expression<Action<ITestTarget>> expression = t => t.TestMethod();
        const string label = "TestLabel";

        // Act
        this.historyKeeperMock.Object.AddChange(label, targetMock.Object, expression);

        // Assert
        this.historyKeeperMock.Verify(
            h => h.AddChange(It.Is<LambdaExpressionOnTarget<ITestTarget>>(a => (string)a.Key == label)),
            Times.Once);
    }

    [TestMethod]
    public void AddChange_WithActionAndArgument_ShouldAddChangeToUndoManager()
    {
        // Arrange
        Action<string?> action = Console.WriteLine;
        const string argument = "TestArgument";
        const string label = "TestLabel";

        // Act
        this.historyKeeperMock.Object.AddChange(label, action, argument);

        // Assert
        this.historyKeeperMock.Verify(
            h => h.AddChange(It.Is<ActionWithArgument<string?>>(a => (string)a.Key == label)),
            Times.Once);
    }

    [TestMethod]
    public void AddChange_WithSimpleAction_ShouldAddChangeToUndoManager()
    {
        // Arrange
        const string label = "TestLabel";

        // Act
        this.historyKeeperMock.Object.AddChange(label, Action);

        // Assert
        this.historyKeeperMock.Verify(
            h => h.AddChange(It.Is<SimpleAction>(a => (string)a.Key == label)),
            Times.Once);
    }

    private static void Action() => Console.WriteLine("Test");
}
