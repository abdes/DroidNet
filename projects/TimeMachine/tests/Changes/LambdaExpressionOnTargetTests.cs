// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Tests.Changes;

using System.Diagnostics.CodeAnalysis;
using System.Linq.Expressions;
using DroidNet.TimeMachine.Changes;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Changes")]
public class LambdaExpressionOnTargetTests
{
    [SuppressMessage("ReSharper", "MemberCanBePrivate.Global", Justification = "used by Moq")]
    public interface ITarget
    {
        void DoSomething();
    }

    [TestMethod]
    public void Apply_ShouldInvokeExpressionOnTarget()
    {
        // Arrange
        var targetMock = new Mock<ITarget>();
        Expression<Action<ITarget>> expression = t => t.DoSomething();
        var change = new LambdaExpressionOnTarget<ITarget>(targetMock.Object, expression) { Key = new object() };

        // Act
        change.Apply();

        // Assert
        targetMock.Verify(t => t.DoSomething(), Times.Once);
    }

    [TestMethod]
    public void Constructor_ShouldInitializeTarget()
    {
        // Arrange
        var targetMock = new Mock<ITarget>();
        Expression<Action<ITarget>> expression = t => t.DoSomething();

        // Act
        var change = new LambdaExpressionOnTarget<ITarget>(targetMock.Object, expression) { Key = new object() };

        // Assert
        change.Target.Should().Be(targetMock.Object);
    }
}
