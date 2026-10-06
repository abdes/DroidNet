// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.TimeMachine.Changes;

namespace DroidNet.TimeMachine.Tests;

public partial class HistoryKeeperAsyncTests
{
    [TestMethod]
    public async Task UndoAsync_UnappliedFailureRetainsStepAndExistingRedoForRetry()
    {
        var history = new HistoryKeeper(new object());
        var fail = true;
        history.AddChange("older", () =>
        {
            if (fail)
            {
                throw new InvalidOperationException("Controlled undo failure");
            }

            history.AddChange("older redo", () => Task.CompletedTask);
            return Task.CompletedTask;
        });
        history.AddChange("newer", () =>
        {
            history.AddChange("newer redo", () => Task.CompletedTask);
            return Task.CompletedTask;
        });
        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var original = history.UndoStack.Single();
        var existingRedo = history.RedoStack.Single();

        Func<Task> undo = async () => await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await undo.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);

        _ = history.State.Should().Be(HistoryKeeper.States.Idle);
        _ = history.UndoStack.Should().ContainSingle().Which.Should().BeSameAs(original);
        _ = history.RedoStack.Should().ContainSingle().Which.Should().BeSameAs(existingRedo);
        fail = false;
        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = history.UndoStack.Should().BeEmpty();
        _ = history.RedoStack.Should().HaveCount(2);
    }

    [TestMethod]
    public async Task RedoAsync_UnappliedChangeSetFailureRetainsStepAndOlderEmptySetForRetry()
    {
        var history = new HistoryKeeper(new object());
        var older = new ChangeSet { Key = "older empty set" };
        history.AddChange(older);
        var fail = true;
        history.BeginChangeSet("batch");
        history.AddChange("undo", () =>
        {
            history.AddChange("redo", () =>
            {
                if (fail)
                {
                    throw new InvalidOperationException("Controlled redo failure");
                }

                history.AddChange("undo again", () => Task.CompletedTask);
                return Task.CompletedTask;
            });
            return Task.CompletedTask;
        });
        history.EndChangeSet();
        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var original = history.RedoStack.Single();

        Func<Task> redo = async () => await history.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await redo.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);

        _ = history.State.Should().Be(HistoryKeeper.States.Idle);
        _ = history.RedoStack.Should().ContainSingle().Which.Should().BeSameAs(original);
        _ = history.UndoStack.Should().ContainSingle().Which.Should().BeSameAs(older);
        fail = false;
        await history.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = history.RedoStack.Should().BeEmpty();
        _ = history.UndoStack.Should().HaveCount(2);
    }

    [TestMethod]
    public async Task UndoAsync_PartialChangeSetFailureKeepsRecordedInverseWithoutRestoringWholeStep()
    {
        var history = new HistoryKeeper(new object());
        history.BeginChangeSet("partial batch");
        history.AddChange("fails second", () => Task.FromException(new InvalidOperationException("Controlled partial failure")));
        history.AddChange("applies first", () =>
        {
            history.AddChange("redo applied part", () => Task.CompletedTask);
            return Task.CompletedTask;
        });
        history.EndChangeSet();

        Func<Task> undo = async () => await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await undo.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);

        _ = history.State.Should().Be(HistoryKeeper.States.Idle);
        _ = history.UndoStack.Should().BeEmpty();
        _ = history.RedoStack.Should().ContainSingle().Which.Should().BeOfType<ChangeSet>()
            .Which.Changes.Should().ContainSingle();
    }
}
