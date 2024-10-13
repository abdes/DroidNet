// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Tests.Changes;

using System.Diagnostics.CodeAnalysis;
using DroidNet.TimeMachine.Changes;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Changes")]
public class ActionWithArgumentTests
{
    [TestMethod]
    public void Apply_ShouldInvokeActionWithArgument()
    {
        // Arrange
        const string key = "simple";
        var mockAction = new Mock<Action<string?>>();
        const string argument = "test";
        var actionWithArgument = new ActionWithArgument<string>(mockAction.Object, argument) { Key = key };

        // Act
        actionWithArgument.Apply();

        // Assert
        mockAction.Verify(a => a(argument), Times.Once);
    }

    [TestMethod]
    public void Apply_ShouldHandleNullArgument()
    {
        // Arrange
        const string key = "simple";
        var mockAction = new Mock<Action<string?>>();
        const string? nullArgument = null;
        var actionWithArgument = new ActionWithArgument<string>(mockAction.Object, nullArgument) { Key = key };

        // Act
        actionWithArgument.Apply();

        // Assert
        mockAction.Verify(a => a(nullArgument), Times.Once);
    }
}
