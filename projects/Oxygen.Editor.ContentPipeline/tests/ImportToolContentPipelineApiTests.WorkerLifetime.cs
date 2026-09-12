// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies manifest cleanup against the worker lifetime.</summary>
public sealed partial class ImportToolContentPipelineApiTests
{
    /// <summary>Retains input until the failed-to-terminate worker actually drains.</summary>
    /// <param name="readerFailed">Whether a reader faults after termination fails.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ImportAsync_TerminationFailureRetainsManifestUntilDrain(bool readerFailed)
    {
        using var workspace = new TempWorkspace();
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var failure = new ContentPipelineTerminationException(new Win32Exception(5), drain.Task);
        var runner = new FailingWorkerRunner(failure);
        var api = new ImportToolContentPipelineApi(
            new FixedToolLocator(workspace.ToolPath), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Qualification);

        var import = async () => await api.ImportAsync(CreateExecution(workspace, CreateManifest(workspace)), CancellationToken.None).ConfigureAwait(false);
        _ = await import.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
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
        while (File.Exists(runner.ManifestPath))
        {
            await Task.Delay(20, timeout.Token).ConfigureAwait(false);
        }
    }

    /// <summary>Cleans operation input when process creation fails.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ImportAsync_StartupFailureCleansManifest()
    {
        using var workspace = new TempWorkspace();
        var runner = new FailingWorkerRunner(new Win32Exception(2));
        var api = new ImportToolContentPipelineApi(
            new FixedToolLocator(workspace.ToolPath), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Qualification);
        var import = async () => await api.ImportAsync(CreateExecution(workspace, CreateManifest(workspace)), CancellationToken.None).ConfigureAwait(false);
        _ = await import.Should().ThrowAsync<Win32Exception>().ConfigureAwait(false);
        _ = File.Exists(runner.ManifestPath).Should().BeFalse();
    }

    private sealed class FailingWorkerRunner(Exception exception) : IContentPipelineProcessRunner
    {
        internal string? ManifestPath { get; private set; }

        public Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
        {
            var index = request.Arguments.ToList().IndexOf("--manifest");
            this.ManifestPath = request.Arguments[index + 1];
            return Task.FromException<ContentPipelineProcessResult>(exception);
        }
    }
}
