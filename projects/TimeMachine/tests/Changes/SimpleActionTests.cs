// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Tests.Changes;

using System.Diagnostics.CodeAnalysis;
using DroidNet.TimeMachine.Changes;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Changes")]
public class SimpleActionTests
{
    [TestMethod]
    public void Apply_ShouldInvokeAction()
    {
        // Arrange
        const string key = "simple";
        var mockAction = new Mock<Action>();
        var simpleAction = new SimpleAction(mockAction.Object) { Key = key };

        // Act
        simpleAction.Apply();

        // Assert
        mockAction.Verify(action => action(), Times.Once);
    }

    [TestMethod]
    public void Constructor_ShouldInitializeKey()
    {
        // Arrange
        var mockAction = new Mock<Action>();
        const string key = "simple";

        // Act
        var simpleAction = new SimpleAction(mockAction.Object) { Key = key };

        // Assert
        simpleAction.Key.Should().Be(key);
    }
}
