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
public class ChangeSetTests
{
    [TestMethod]
    public void Apply_ShouldInvokeApplyOnAllChanges()
    {
        // Arrange
        var changeMock1 = new Mock<IChange>();
        var changeMock2 = new Mock<IChange>();
        var changeSet = new ChangeSet { Key = new object() };
        changeSet.Add(changeMock1.Object);
        changeSet.Add(changeMock2.Object);

        // Act
        changeSet.Apply();

        // Assert
        changeMock1.Verify(c => c.Apply(), Times.Once);
        changeMock2.Verify(c => c.Apply(), Times.Once);
    }

    [TestMethod]
    public void Add_ShouldAddChangeToChangeSet()
    {
        // Arrange
        var changeMock = new Mock<IChange>();
        var changeSet = new ChangeSet { Key = new object() };

        // Act
        changeSet.Add(changeMock.Object);

        // Assert
        changeSet.Changes.Should().Contain(changeMock.Object);
    }

    [TestMethod]
    public void Changes_ShouldReturnAllAddedChanges()
    {
        // Arrange
        var changeMock1 = new Mock<IChange>();
        var changeMock2 = new Mock<IChange>();
        var changeSet = new ChangeSet { Key = new object() };
        changeSet.Add(changeMock1.Object);
        changeSet.Add(changeMock2.Object);

        // Act
        var changes = changeSet.Changes;

        // Assert
        changes.Should().Contain(new[] { changeMock1.Object, changeMock2.Object });
    }
}
