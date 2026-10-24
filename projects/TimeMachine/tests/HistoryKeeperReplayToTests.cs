// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;

namespace DroidNet.TimeMachine.Tests;

/// <summary>Verifies undoing or redoing to an entry of the history as one operation.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed class HistoryKeeperReplayToTests
{
    private int value;

    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Replaying to a count undoes or redoes every entry between, back and forth.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReplayToUndoesAndRedoesEveryEntryBetween()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        this.Set(history, 2);
        this.Set(history, 3);

        await history.ReplayToAsync(1, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = this.value.Should().Be(1);
        _ = history.UndoStack.Should().ContainSingle();
        _ = history.RedoStack.Should().HaveCount(2);

        await history.ReplayToAsync(3, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = this.value.Should().Be(3);
        _ = history.UndoStack.Should().HaveCount(3);
        _ = history.RedoStack.Should().BeEmpty();

        await history.ReplayToAsync(0, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = this.value.Should().Be(0);
        _ = history.UndoStack.Should().BeEmpty();
        _ = history.RedoStack.Should().HaveCount(3);
    }

    /// <summary>
    /// A change refused part way stops the replay there: the entries before it stay replayed, the refused change stays
    /// on its stack, the history is idle, and the replay can run again once the change applies.
    /// </summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task RefusedChangeStopsTheReplayAndStaysOnItsStack()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        var refuse = true;
        this.value = 2;
        history.AddChange("Set 2", () =>
        {
            if (refuse)
            {
                throw new InvalidOperationException("Controlled refusal");
            }

            this.Set(history, 1);
            return Task.CompletedTask;
        });
        this.Set(history, 3);
        var refused = history.UndoStack[1];

        Func<Task> replay = async () => await history.ReplayToAsync(0, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await replay.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);

        _ = this.value.Should().Be(2);
        _ = history.IsBusy.Should().BeFalse();
        _ = history.UndoStack.Should().HaveCount(2);
        _ = history.UndoStack[^1].Should().BeSameAs(refused);
        _ = history.RedoStack.Should().ContainSingle();

        refuse = false;
        await history.ReplayToAsync(0, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = this.value.Should().Be(0);
        _ = history.UndoStack.Should().BeEmpty();
        _ = history.RedoStack.Should().HaveCount(3);
    }

    /// <summary>
    /// The history is busy for the whole replay: an undo asked for meanwhile is ignored and clearing the history is
    /// refused.
    /// </summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task HistoryIsBusyForTheWholeReplay()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        var complete = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        history.AddChange("Deferred", async () =>
        {
            await complete.Task.ConfigureAwait(false);
            history.AddChange("Deferred", () => Task.CompletedTask);
        });

        var replay = history.ReplayToAsync(0, this.TestContext.CancellationToken);
        _ = history.IsBusy.Should().BeTrue();
        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var clear = history.Clear;
        _ = clear.Should().Throw<InvalidOperationException>();
        complete.SetResult();
        await replay.ConfigureAwait(false);

        _ = history.IsBusy.Should().BeFalse();
        _ = history.UndoStack.Should().BeEmpty();
        _ = this.value.Should().Be(0);
    }

    /// <summary>Cancellation stops the replay before its next entry.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CancellationStopsBeforeTheNextEntry()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        using var cancellation = new CancellationTokenSource();
        this.value = 2;
        history.AddChange("Set 2", () =>
        {
            this.Set(history, 1);
            cancellation.Cancel();
            return Task.CompletedTask;
        });

        Func<Task> replay = async () => await history.ReplayToAsync(0, cancellation.Token).ConfigureAwait(false);
        _ = await replay.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);

        _ = this.value.Should().Be(1);
        _ = history.UndoStack.Should().ContainSingle();
        _ = history.IsBusy.Should().BeFalse();
    }

    /// <summary>A count below zero or beyond both stacks is refused before anything is replayed.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CountOutsideTheHistoryIsRefused()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);

        Func<Task> negative = async () => await history.ReplayToAsync(-1, this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> beyond = async () => await history.ReplayToAsync(2, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await negative.Should().ThrowAsync<ArgumentOutOfRangeException>().ConfigureAwait(false);
        _ = await beyond.Should().ThrowAsync<ArgumentOutOfRangeException>().ConfigureAwait(false);
        _ = this.value.Should().Be(1);
    }

    /// <summary>A change set is one entry: replaying to a count undoes and redoes it whole.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ChangeSetReplaysAsOneEntry()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        history.BeginChangeSet("Group");
        this.Set(history, 2);
        this.Set(history, 3);
        history.EndChangeSet();

        await history.ReplayToAsync(1, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = this.value.Should().Be(1);
        _ = history.UndoStack.Should().ContainSingle();
        _ = history.RedoStack.Should().ContainSingle();

        await history.ReplayToAsync(2, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = this.value.Should().Be(3);
        _ = history.UndoStack.Should().HaveCount(2);
        _ = history.RedoStack.Should().BeEmpty();
    }

    /// <summary>Replaying to the count the undo stack already holds changes nothing.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReplayToCurrentCountDoesNothing()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        this.Set(history, 2);
        await history.ReplayToAsync(1, this.TestContext.CancellationToken).ConfigureAwait(false);
        var position = history.Position;

        await history.ReplayToAsync(1, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = history.Position.Should().Be(position);
        _ = history.UndoStack.Should().ContainSingle();
        _ = history.RedoStack.Should().ContainSingle();
        _ = this.value.Should().Be(1);
    }

    /// <summary>
    /// A second replay asked for while one runs is ignored, whatever its upper bound, since the stacks are between states; a
    /// negative count is still refused.
    /// </summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReplayToWhileReplayingIsIgnored()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        this.Set(history, 2);
        var complete = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        history.AddChange("Deferred", async () =>
        {
            await complete.Task.ConfigureAwait(false);
            history.AddChange("Deferred", () => Task.CompletedTask);
        });

        var replay = history.ReplayToAsync(0, this.TestContext.CancellationToken);
        await history.ReplayToAsync(1, this.TestContext.CancellationToken).ConfigureAwait(false);
        await history.ReplayToAsync(99, this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> negative = async () => await history.ReplayToAsync(-1, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await negative.Should().ThrowAsync<ArgumentOutOfRangeException>().ConfigureAwait(false);
        complete.SetResult();
        await replay.ConfigureAwait(false);

        _ = history.UndoStack.Should().BeEmpty();
        _ = history.RedoStack.Should().HaveCount(3);
        _ = this.value.Should().Be(0);
    }

    /// <summary>
    /// An undone change that records no inverse stops the replay and is reported by its key; the entries replayed before
    /// it stay replayed.
    /// </summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task UndoWithoutInverseStopsAndNamesTheEntry()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        history.AddChange("Defective", () => Task.CompletedTask);
        this.Set(history, 2);

        Func<Task> replay = async () => await history.ReplayToAsync(0, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await replay.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false))
            .Which.Message.Should().Contain("Defective").And.Contain("recorded 0 inverse");

        _ = history.IsBusy.Should().BeFalse();
        _ = history.UndoStack.Should().ContainSingle();
        _ = history.RedoStack.Should().ContainSingle();
    }

    /// <summary>A redone change that records no inverse stops the replay and is reported by its key.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task RedoWithoutInverseStopsAndNamesTheEntry()
    {
        var history = new HistoryKeeper(this);
        history.AddChange("Defective", () =>
        {
            history.AddChange("Defective", () => Task.CompletedTask);
            return Task.CompletedTask;
        });
        this.Set(history, 1);
        await history.ReplayToAsync(0, this.TestContext.CancellationToken).ConfigureAwait(false);

        Func<Task> replay = async () => await history.ReplayToAsync(2, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await replay.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false))
            .Which.Message.Should().Contain("Defective").And.Contain("recorded 0 inverse");

        _ = history.IsBusy.Should().BeFalse();
        _ = history.UndoStack.Should().BeEmpty();
        _ = history.RedoStack.Should().ContainSingle();
    }

    private void Set(HistoryKeeper history, int next)
    {
        var previous = this.value;
        this.value = next;
        history.AddChange(string.Create(CultureInfo.InvariantCulture, $"Set {next}"), () =>
        {
            this.Set(history, previous);
            return Task.CompletedTask;
        });
    }
}
