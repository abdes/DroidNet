// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Tests;

using System.Diagnostics.CodeAnalysis;
using DroidNet.TimeMachine.Changes;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(HistoryKeeper)}.ChangeSets")]
public class HistoryKeeperChangeSetTests
{
    private readonly HistoryKeeper historyKeeper = new(new object());
    private int counter;

    [TestMethod]
    public void BeginChangeSet_GroupsSubsequentChangesInUndoStack_WhileIdle()
    {
        // Arrange
        this.historyKeeper.State.Should().Be(HistoryKeeper.States.Idle);

        // Act
        this.BatchAction();

        // Assert
        this.counter.Should().Be(3);
        this.historyKeeper.UndoStack.Should()
            .ContainSingle()
            .Which.Should()
            .BeOfType<ChangeSet>()
            .And.Match(cs => cs.As<ChangeSet>().Changes.Take(4).Count() == 3);
    }

    [TestMethod]
    public void BeginChangeSet_GroupsSubsequentChangesInUndoStack_WhileRedoing()
    {
        // Arrange
        this.historyKeeper.State.Should().Be(HistoryKeeper.States.Idle);
        this.ActionWithBatchReverseAction();
        this.historyKeeper.Undo();
        this.historyKeeper.RedoStack.Should().HaveCount(1);

        // Act
        this.historyKeeper.Redo();

        // Assert
        this.counter.Should().Be(3);
        this.historyKeeper.UndoStack.Should()
            .ContainSingle()
            .Which.Should()
            .BeOfType<ChangeSet>()
            .And.Match(cs => cs.As<ChangeSet>().Changes.Take(4).Count() == 3);
    }

    [TestMethod]
    public void BeginChangeSet_GroupsSubsequentChangesInRedoStack_WhileUndoing()
    {
        // Arrange
        this.historyKeeper.State.Should().Be(HistoryKeeper.States.Idle);
        this.BatchAction();

        // Act
        this.historyKeeper.Undo();

        // Assert
        this.counter.Should().Be(0);
        this.historyKeeper.RedoStack.Should()
            .ContainSingle()
            .Which.Should()
            .BeOfType<ChangeSet>()
            .And.Match(cs => cs.As<ChangeSet>().Changes.Take(4).Count() == 3);
    }

    [TestMethod]
    public void BeginChangeSet_SupportsNesting()
    {
        // Arrange
        this.historyKeeper.State.Should().Be(HistoryKeeper.States.Idle);

        // Act
        this.historyKeeper.BeginChangeSet("many changes");
        {
            this.Action();
            this.Action();

            this.historyKeeper.BeginChangeSet("many changes");
            {
                this.Action();
                this.historyKeeper.EndChangeSet();
            }

            this.historyKeeper.EndChangeSet();
        }

        // Assert
        this.counter.Should().Be(3);
        this.historyKeeper.UndoStack.Should()
            .ContainSingle()
            .Which.Should()
            .BeOfType<ChangeSet>()
            .And.Match(cs => cs.As<ChangeSet>().Changes.Take(4).Count() == 3);
    }

    [TestMethod]
    public void EndChangeSet_CanBeCalledTooManyTimes()
    {
        // Arrange
        this.historyKeeper.State.Should().Be(HistoryKeeper.States.Idle);
        this.historyKeeper.BeginChangeSet("many changes");
        {
            this.Action();
            this.Action();
            this.Action();

            this.historyKeeper.EndChangeSet();
        }

        // Act
        var act = this.historyKeeper.EndChangeSet;

        // Assert
        act.Should().NotThrow();
    }

    private void ReverseAction()
    {
        this.counter--;
        this.historyKeeper.AddChange("redo = set", this.Action);
    }

    private void Action()
    {
        this.counter++;
        this.historyKeeper.AddChange("undo = reset", this.ReverseAction);
    }

    private void BatchAction()
    {
        this.historyKeeper.BeginChangeSet("many changes");

        this.Action();
        this.Action();
        this.Action();

        this.historyKeeper.EndChangeSet();
    }

    private void BatchReverseAction()
    {
        this.historyKeeper.BeginChangeSet("many changes");

        this.ReverseAction();
        this.ReverseAction();
        this.ReverseAction();

        this.historyKeeper.EndChangeSet();
    }

    private void ActionWithBatchReverseAction()
    {
        this.counter = 3;
        this.historyKeeper.AddChange("undo = reset", this.BatchReverseAction);
    }
}
