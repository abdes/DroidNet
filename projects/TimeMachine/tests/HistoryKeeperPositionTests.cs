// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using AwesomeAssertions;
using DroidNet.TimeMachine.Changes;

namespace DroidNet.TimeMachine.Tests;

/// <summary>Tests that a history's position names the state its root is in.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("HistoryKeeper.Position")]
public class HistoryKeeperPositionTests
{
    private int value;

    public TestContext TestContext { get; set; }

    [TestMethod]
    public void Position_AfterNewEntry_IsNew()
    {
        var history = new HistoryKeeper(this);
        var start = history.Position;

        this.Set(history, 1);
        var first = history.Position;
        this.Set(history, 2);

        _ = first.Should().NotBe(start);
        _ = history.Position.Should().NotBe(first).And.NotBe(start);
    }

    [TestMethod]
    public void Position_AfterUndoAndRedo_ReturnsToTheStatesTheyLeft()
    {
        var history = new HistoryKeeper(this);
        var start = history.Position;
        this.Set(history, 1);
        var first = history.Position;
        this.Set(history, 2);
        var second = history.Position;

        _ = history.Undo();
        _ = history.Position.Should().Be(first);
        _ = history.Undo();
        _ = history.Position.Should().Be(start);
        _ = history.Redo();
        _ = history.Position.Should().Be(first);
        _ = history.Redo();
        _ = history.Position.Should().Be(second);
        _ = this.value.Should().Be(2);
    }

    [TestMethod]
    public async Task Position_AfterUndoAsyncAndRedoAsync_ReturnsToTheStatesTheyLeftAsync()
    {
        var history = new HistoryKeeper(this);
        var start = history.Position;
        this.SetWithAsyncInverse(history, 1);
        var first = history.Position;

        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = history.Position.Should().Be(start);
        await history.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = history.Position.Should().Be(first);
    }

    [TestMethod]
    public void Position_AfterNewEntryOverUndone_NeverReturnsToTheUndoneState()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        var first = history.Position;
        this.Set(history, 2);
        var second = history.Position;

        _ = history.Undo();
        this.Set(history, 3);

        _ = history.Position.Should().NotBe(second).And.NotBe(first);
        _ = history.Undo();
        _ = history.Position.Should().Be(first);
        _ = history.CanRedo.Should().BeTrue();
        _ = history.Redo();
        _ = history.Position.Should().NotBe(second);
    }

    [TestMethod]
    public void Position_OfChangeSet_IsOneState()
    {
        var history = new HistoryKeeper(this);
        var start = history.Position;

        history.BeginChangeSet("both");
        this.Set(history, 1);
        this.Set(history, 2);
        history.EndChangeSet();
        var after = history.Position;

        _ = history.Undo();
        _ = history.Position.Should().Be(start);
        _ = history.Redo();
        _ = history.Position.Should().Be(after);
    }

    [TestMethod]
    public void Position_AfterClear_IsNew()
    {
        var history = new HistoryKeeper(this);
        var start = history.Position;
        this.Set(history, 1);

        history.Clear();

        _ = history.Position.Should().NotBe(start);
        _ = history.CanUndo.Should().BeFalse();
    }

    [TestMethod]
    public void Position_SeenByUndoStackObservers_IsAlreadyUpdated()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        var first = history.Position;
        this.Set(history, 2);
        var seen = new List<HistoryPosition>();
        ((INotifyCollectionChanged)history.UndoStack).CollectionChanged += (_, _) => seen.Add(history.Position);

        _ = history.Undo();

        _ = seen.Should().NotBeEmpty().And.OnlyContain(position => position == first);
    }

    [TestMethod]
    public async Task Position_AfterFailedUndoAsync_IsUnchangedAsync()
    {
        var history = new HistoryKeeper(this);
        history.AddChange("fail", () => Task.FromException(new InvalidOperationException("undo failed")));
        var position = history.Position;

        var undo = async () => await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await undo.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
        _ = history.Position.Should().Be(position);
        _ = history.CanUndo.Should().BeTrue();
    }

    [TestMethod]
    public void Position_OfAChangeThatKeepsTheState_DoesNotMove()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        var first = history.Position;

        Present(history, "Hide");
        _ = history.Position.Should().Be(first);
        _ = history.Undo();
        _ = history.Position.Should().Be(first);
        _ = history.Redo();
        _ = history.Position.Should().Be(first);
        _ = history.Undo();
        _ = history.Undo();

        _ = history.Position.Should().NotBe(first);
    }

    private static void Present(HistoryKeeper history, string label)
        => history.AddChange(new SimpleAction(() => Present(history, label)) { Key = label, ChangesState = false });

    private void Set(HistoryKeeper history, int next)
    {
        var previous = this.value;
        this.value = next;
        history.AddChange(string.Create(CultureInfo.InvariantCulture, $"Set {next}"), () => this.Set(history, previous));
    }

    private void SetWithAsyncInverse(HistoryKeeper history, int next)
    {
        var previous = this.value;
        this.value = next;
        history.AddChange(string.Create(CultureInfo.InvariantCulture, $"Set {next}"), async () =>
        {
            await Task.Yield();
            this.SetWithAsyncInverse(history, previous);
        });
    }
}
