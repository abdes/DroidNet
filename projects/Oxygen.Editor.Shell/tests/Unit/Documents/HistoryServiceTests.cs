// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;
using DroidNet.TimeMachine;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

[TestClass]
public sealed class HistoryServiceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A change recorded into the global history undoes and redoes through it.</summary>
    /// <returns>The asynchronous undo regression.</returns>
    [TestMethod]
    public async Task GlobalHistoryUndoesAndRedoesItsChanges()
    {
        using var service = new HistoryService();
        var value = 1;
        Record(service.GlobalHistory, from: 1, to: 2);
        value = 2;

        await service.GlobalHistory.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = value.Should().Be(1);
        _ = service.GlobalHistory.CanRedo.Should().BeTrue();

        await service.GlobalHistory.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = value.Should().Be(2);
        _ = service.GlobalHistory.CanUndo.Should().BeTrue();

        void Record(HistoryKeeper history, int from, int to)
            => history.AddChange(string.Create(CultureInfo.InvariantCulture, $"Set {to}"), () =>
            {
                value = from;
                Record(history, to, from);
            });
    }

    /// <summary>Ending the session drops its history.</summary>
    [TestMethod]
    public void EndingTheSessionDropsTheHistory()
    {
        var service = new HistoryService();
        service.GlobalHistory.AddChange("Rename", () => { });

        service.Dispose();

        _ = service.GlobalHistory.CanUndo.Should().BeFalse();
    }
}
