// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace DroidNet.TimeMachine.Tests;

/// <summary>Verifies the public state used before replacing a document's history.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed class HistoryKeeperBusyTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>An asynchronous undo remains busy until its complete replay has finished.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task IsBusyRemainsTrueUntilUndoCompletes()
    {
        var history = new HistoryKeeper(new object());
        var complete = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        history.AddChange("Deferred change", () => complete.Task);
        var undo = history.UndoAsync(this.TestContext.CancellationToken);
        _ = history.IsBusy.Should().BeTrue();
        complete.SetResult();
        await undo.ConfigureAwait(false);
        _ = history.IsBusy.Should().BeFalse();
    }

    /// <summary>An open change set is busy even before its first change has been recorded.</summary>
    [TestMethod]
    public void IsBusyIncludesAnOpenChangeSet()
    {
        var history = new HistoryKeeper(new object());
        history.BeginChangeSet("Batch");
        _ = history.IsBusy.Should().BeTrue();
        history.EndChangeSet();
        _ = history.IsBusy.Should().BeFalse();
    }
}
