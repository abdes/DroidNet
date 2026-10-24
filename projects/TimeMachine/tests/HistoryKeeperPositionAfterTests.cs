// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;

namespace DroidNet.TimeMachine.Tests;

/// <summary>Verifies the position a history is at after a number of its oldest entries.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed class HistoryKeeperPositionAfterTests
{
    private int value;

    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The start, done and undone positions match the position reported after replaying to each count.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task PositionAfter_MatchesPositionOfTheHistoryReplayedToEachCount()
    {
        var history = new HistoryKeeper(this);
        var expected = new List<HistoryPosition> { history.Position };
        for (var next = 1; next <= 3; next++)
        {
            this.Set(history, next);
            expected.Add(history.Position);
        }

        await history.ReplayToAsync(1, this.TestContext.CancellationToken).ConfigureAwait(false);

        for (var count = 0; count <= 3; count++)
        {
            _ = history.PositionAfter(count).Should().Be(expected[count]);
        }

        for (var count = 3; count >= 0; count--)
        {
            await history.ReplayToAsync(count, this.TestContext.CancellationToken).ConfigureAwait(false);

            _ = history.Position.Should().Be(history.PositionAfter(count));
            _ = history.Position.Should().Be(expected[count]);
        }
    }

    /// <summary>A change set counts as one entry.</summary>
    [TestMethod]
    public void PositionAfter_CountsAChangeSetAsOneEntry()
    {
        var history = new HistoryKeeper(this);
        var start = history.Position;
        this.Set(history, 1);
        var first = history.Position;
        history.BeginChangeSet("Group");
        this.Set(history, 2);
        this.Set(history, 3);
        history.EndChangeSet();

        _ = history.PositionAfter(0).Should().Be(start);
        _ = history.PositionAfter(1).Should().Be(first);
        _ = history.PositionAfter(2).Should().Be(history.Position);
    }

    /// <summary>Clearing the history moves its start to a position no earlier one equals.</summary>
    [TestMethod]
    public void PositionAfter_AfterClear_IsTheNewStart()
    {
        var history = new HistoryKeeper(this);
        var before = history.PositionAfter(0);
        this.Set(history, 1);

        history.Clear();

        _ = history.PositionAfter(0).Should().Be(history.Position).And.NotBe(before);
    }

    /// <summary>A count below zero or beyond both stacks is refused.</summary>
    [TestMethod]
    public void PositionAfter_OutsideTheHistory_IsRefused()
    {
        var history = new HistoryKeeper(this);
        this.Set(history, 1);
        _ = history.Undo();

        var negative = () => history.PositionAfter(-1);
        var beyond = () => history.PositionAfter(2);

        _ = negative.Should().Throw<ArgumentOutOfRangeException>();
        _ = beyond.Should().Throw<ArgumentOutOfRangeException>();
        _ = history.PositionAfter(1).Should().NotBe(history.PositionAfter(0));
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
