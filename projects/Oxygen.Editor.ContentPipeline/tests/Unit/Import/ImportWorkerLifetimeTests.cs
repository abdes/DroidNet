// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using static Oxygen.Editor.ContentPipeline.TestSupport.ImportAdapterScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class ImportWorkerLifetimeTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Retains input until the failed-to-terminate worker actually drains.</summary>
    /// <param name="readerFailed">Whether a reader faults after termination fails.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ImportAsync_TerminationFailureRetainsManifestUntilDrain(bool readerFailed)
    {
        using var workspace = new ImportAdapterWorkspace();
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var failure = new ContentPipelineTerminationException(new Win32Exception(5), drain.Task);
        var runner = new FailingWorkerRunner(failure);
        var api = new ImportToolContentPipelineApi(
            new FixedToolLocator(workspace.ToolPath), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Compatibility);

        var import = async () => await api.ImportAsync(CreateExecution(workspace, CreateManifest(workspace)), CancellationToken.None).ConfigureAwait(false);
        var thrown = await import.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
        _ = File.Exists(runner.ManifestPath).Should().BeTrue();
        if (readerFailed)
        {
            drain.SetException(new IOException("reader failed after worker termination"));
        }
        else
        {
            drain.SetResult();
        }

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        await thrown.Which.DrainCompletion.WaitAsync(timeout.Token).ConfigureAwait(false);
        _ = File.Exists(runner.ManifestPath).Should().BeFalse();
    }

    /// <summary>Cleans operation input when process creation fails.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ImportAsync_StartupFailureCleansManifest()
    {
        using var workspace = new ImportAdapterWorkspace();
        var runner = new FailingWorkerRunner(new Win32Exception(2));
        var api = new ImportToolContentPipelineApi(
            new FixedToolLocator(workspace.ToolPath), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Compatibility);
        var import = async () => await api.ImportAsync(CreateExecution(workspace, CreateManifest(workspace)), CancellationToken.None).ConfigureAwait(false);
        _ = await import.Should().ThrowAsync<Win32Exception>().ConfigureAwait(false);
        _ = File.Exists(runner.ManifestPath).Should().BeFalse();
    }
}
