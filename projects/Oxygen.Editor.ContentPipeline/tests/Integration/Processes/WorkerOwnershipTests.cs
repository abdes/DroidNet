// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Processes;
using Oxygen.Editor.ContentPipeline.TestSupport;
using static Oxygen.Editor.ContentPipeline.TestSupport.ContentPipelineProcessRunnerScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Processes;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class WorkerOwnershipTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Limits termination to the operation-owned job.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunAsync_CancellingOneJobDoesNotStopAnotherWorker()
    {
        var first = new WorkerScope();
        await using var firstLifetime = first.ConfigureAwait(false);
        var second = new WorkerScope();
        await using var secondLifetime = second.ConfigureAwait(false);
        var cancelledOperation = first.Run(["write", first.Root]);
        var unaffectedOperation = second.Run(["write", second.Root]);
        var firstWrites = Path.Combine(first.Root, "root.writes");
        var secondWrites = Path.Combine(second.Root, "root.writes");
        await WaitUntilAsync(() => File.Exists(firstWrites) && File.Exists(secondWrites)).ConfigureAwait(false);
        await first.Cancellation.CancelAsync().ConfigureAwait(false);
        var cancel = async () => await cancelledOperation.ConfigureAwait(false);
        _ = await cancel.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        var length = new FileInfo(secondWrites).Length;
        await WaitUntilAsync(() => new FileInfo(secondWrites).Length > length).ConfigureAwait(false);
        _ = unaffectedOperation.IsCompleted.Should().BeFalse();
    }
}
