// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Moq;

namespace DroidNet.TimeMachine.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("StateTransition")]
public class StateTransitionTests
{
    [TestMethod]
    public void Constructor_ShouldThrowInvalidOperationException_WhenTargetIsNull()
    {
        // Arrange
        IStateful? target = null;
        var newState = new object();

        // Act
        Action act = () => _ = new StateTransition<object>(target!, newState);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void Constructor_ShouldThrowInvalidOperationException_WhenTargetStateIsNull()
    {
        // Arrange
        var mockTarget = new Mock<IStateful>();
        _ = mockTarget.Setup(t => t.State).Returns((object?)null);
        var newState = new object();

        // Act
        Action act = () => _ = new StateTransition<object>(mockTarget.Object, newState);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void Constructor_ShouldThrowInvalidOperationException_WhenTargetHasNoStateProperty()
    {
        // Arrange
        var newState = new object();

        // Act
        Action act = () => _ = new StateTransition<object>(new object(), newState);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void Constructor_ShouldInvalidOperationException_WhenNewStateIsNull()
    {
        // Arrange
        var mockTarget = new Mock<IStateful>();
        _ = mockTarget.Setup(t => t.State).Returns(new object());
        object? newState = null;

        // Act
        Action act = () => _ = new StateTransition<object>(mockTarget.Object, newState!);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void Dispose_ShouldRestorePreviousState()
    {
        // Arrange
        var previousState = new object();
        var newState = new object();
        var mockTarget = new Mock<IStateful>();
        _ = mockTarget.SetupProperty(t => t.State, previousState);

        using (new StateTransition<object>(mockTarget.Object, newState))
        {
            // Do some stuff...
        }

        // Assert
        _ = mockTarget.Object.State.Should().Be(previousState);
    }

    [TestMethod]
    public void Dispose_ShouldNotThrow_WhenCalledMultipleTimes()
    {
        // Arrange
        var previousState = new object();
        var newState = new object();
        var mockTarget = new Mock<IStateful>();
        _ = mockTarget.SetupProperty(t => t.State, previousState);

        // Act
        var act = () =>
        {
            var transition = new StateTransition<object>(mockTarget.Object, newState);
            transition.Dispose();
            transition.Dispose();
        };

        // Assert
        _ = act.Should().NotThrow();
    }
}
