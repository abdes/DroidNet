// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Tests;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("StateTransition")]
public class StateTransitionTests
{
    [SuppressMessage("ReSharper", "MemberCanBePrivate.Global", Justification = "used for mocking")]
    public interface IStateful
    {
        object? State { get; set; }
    }

    [TestMethod]
    public void Constructor_ShouldThrowInvalidOperationException_WhenTargetIsNull()
    {
        // Arrange
        IStateful? target = null;
        var newState = new object();

        // Act
        Action act = () => _ = new StateTransition<object>(target!, newState);

        // Assert
        act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void Constructor_ShouldThrowInvalidOperationException_WhenTargetStateIsNull()
    {
        // Arrange
        var mockTarget = new Mock<IStateful>();
        mockTarget.Setup(t => t.State).Returns((object?)null);
        var newState = new object();

        // Act
        Action act = () => _ = new StateTransition<object>(mockTarget.Object, newState);

        // Assert
        act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void Constructor_ShouldThrowInvalidOperationException_WhenTargetHasNoStateProperty()
    {
        // Arrange
        var newState = new object();

        // Act
        Action act = () => _ = new StateTransition<object>(new object(), newState);

        // Assert
        act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void Constructor_ShouldInvalidOperationException_WhenNewStateIsNull()
    {
        // Arrange
        var mockTarget = new Mock<IStateful>();
        mockTarget.Setup(t => t.State).Returns(new object());
        object? newState = null;

        // Act
        Action act = () => _ = new StateTransition<object>(mockTarget.Object, newState!);

        // Assert
        act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void Dispose_ShouldRestorePreviousState()
    {
        // Arrange
        var previousState = new object();
        var newState = new object();
        var mockTarget = new Mock<IStateful>();
        mockTarget.SetupProperty(t => t.State, previousState);

        using (new StateTransition<object>(mockTarget.Object, newState))
        {
            // Do some stuff...
        }

        // Assert
        mockTarget.Object.State.Should().Be(previousState);
    }

    [TestMethod]
    public void Dispose_ShouldNotThrow_WhenCalledMultipleTimes()
    {
        // Arrange
        var previousState = new object();
        var newState = new object();
        var mockTarget = new Mock<IStateful>();
        mockTarget.SetupProperty(t => t.State, previousState);

        var transition = new StateTransition<object>(mockTarget.Object, newState);

        // Act
        var act = () =>
        {
            transition.Dispose();
            transition.Dispose();
        };

        // Assert
        act.Should().NotThrow();
    }
}
