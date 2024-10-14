// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Tests;

using DroidNet.TimeMachine.Changes;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

/// <summary>
/// Unit test cases for <see cref="HistoryKeeper" /> class, covering adding changes.
/// </summary>
public partial class HistoryKeeperTests
{
    [TestMethod]
    [TestCategory("HistoryKeeper.Changes")]
    public void AddChange_Should_Add_To_UndoStack_When_Not_Undoing()
    {
        // Arrange
        var changeMock = new Mock<IChange>();
        var historyKeeper = UndoRedo.Default[changeMock];
        historyKeeper.State = HistoryKeeper.States.Idle;

        // Act
        historyKeeper.AddChange(changeMock.Object);

        // Assert
        historyKeeper.UndoStack.Should().Contain(changeMock.Object);
        historyKeeper.RedoStack.Should().BeEmpty();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Changes")]
    public void AddChange_Should_Add_To_RedoStack_When_Undoing()
    {
        // Arrange
        var changeMock = new Mock<IChange>();
        var historyKeeper = UndoRedo.Default[changeMock];
        historyKeeper.State = HistoryKeeper.States.Undoing;

        // Act
        historyKeeper.AddChange(changeMock.Object);

        // Assert
        historyKeeper.RedoStack.Should().Contain(changeMock.Object);
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Changes")]
    public void Clear_Should_Empty_Undo_And_Redo_Stacks()
    {
        // Arrange
        var changeMock = new Mock<IChange>();
        var historyKeeper = UndoRedo.Default[changeMock];
        historyKeeper.State = HistoryKeeper.States.Idle;
        historyKeeper.AddChange(changeMock.Object);

        // Act
        historyKeeper.Clear();

        // Assert
        historyKeeper.UndoStack.Should().BeEmpty();
        historyKeeper.RedoStack.Should().BeEmpty();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Changes")]
    public void Clear_Should_Throw_Exception_When_Not_Idle()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];
        historyKeeper.State = HistoryKeeper.States.Undoing;

        // Act
        var act = historyKeeper.Clear;

        // Assert
        act.Should()
            .Throw<InvalidOperationException>()
            .WithMessage("unable to clear the undo history because we're not Idle");
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Changes")]
    public void CanUndo_Should_Be_True_When_UndoStack_Has_Items()
    {
        // Arrange
        var changeMock = new Mock<IChange>();
        var historyKeeper = UndoRedo.Default[changeMock];
        historyKeeper.State = HistoryKeeper.States.Idle;
        historyKeeper.AddChange(changeMock.Object);

        // Act
        var canUndo = historyKeeper.CanUndo;

        // Assert
        canUndo.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Changes")]
    public void CanUndo_Should_Be_False_When_UndoStack_Is_Empty()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];

        // Act
        var canUndo = historyKeeper.CanUndo;

        // Assert
        canUndo.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Changes")]
    public void CanRedo_Should_Be_True_When_RedoStack_Has_Items()
    {
        // Arrange
        var changeMock = new Mock<IChange>();
        var historyKeeper = UndoRedo.Default[changeMock];
        historyKeeper.State = HistoryKeeper.States.Undoing;
        historyKeeper.AddChange(changeMock.Object);

        // Act
        var canRedo = historyKeeper.CanRedo;

        // Assert
        canRedo.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory("HistoryKeeper.Changes")]
    public void CanRedo_Should_Be_False_When_RedoStack_Is_Empty()
    {
        // Arrange
        var historyKeeper = UndoRedo.Default[new object()];

        // Act
        var canRedo = historyKeeper.CanRedo;

        // Assert
        canRedo.Should().BeFalse();
    }
}
