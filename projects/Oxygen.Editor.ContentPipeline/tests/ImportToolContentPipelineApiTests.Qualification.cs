// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Proves native workers cannot start on mismatched artifacts.</summary>
public sealed partial class ImportToolContentPipelineApiTests
{
    /// <summary>Changed tool bytes block both native import and catalog discovery.</summary>
    /// <param name="catalog">Whether to exercise catalog discovery.</param>
    /// <returns>The asynchronous qualification test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task MismatchedToolNeverLaunchesWorker(bool catalog)
    {
        using var workspace = new TempWorkspace();
        await File.WriteAllTextAsync(workspace.ToolPath, "Changed after qualification", this.TestContext.CancellationToken).ConfigureAwait(false);
        var runner = new CapturingRunner(new(0, string.Empty, string.Empty));
        var api = new ImportToolContentPipelineApi(new FixedToolLocator(workspace.ToolPath), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Qualification);
        if (catalog)
        {
            Func<Task> discover = () => api.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", this.TestContext.CancellationToken);
            var failure = await discover.Should().ThrowAsync<ArtifactQualificationException>().ConfigureAwait(false);
            _ = failure.Which.Diagnostics.Should().ContainSingle(item => item.Code == ArtifactQualificationDiagnosticCodes.ArtifactMismatch);
        }
        else
        {
            var result = await api.ImportAsync(CreateExecution(workspace, CreateManifest(workspace)), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.Succeeded.Should().BeFalse();
            _ = result.Diagnostics.Should().ContainSingle(item => item.Code == ArtifactQualificationDiagnosticCodes.ArtifactMismatch);
        }

        _ = runner.Request.Should().BeNull();
    }

    /// <summary>Failed termination retains qualification leases until the worker/reader drain completes.</summary>
    /// <returns>The asynchronous worker ownership test.</returns>
    [TestMethod]
    public async Task QualificationLeaseSurvivesWorkerTerminationFailure()
    {
        using var workspace = new TempWorkspace();
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var runner = new FailingWorkerRunner(new ContentPipelineTerminationException(new IOException("Termination failed"), drain.Task));
        var api = new ImportToolContentPipelineApi(new FixedToolLocator(workspace.ToolPath), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Qualification);
        Func<Task> import = () => api.ImportAsync(CreateExecution(workspace, CreateManifest(workspace)), this.TestContext.CancellationToken);
        _ = await import.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
        Action overwrite = () => File.WriteAllText(workspace.ToolPath, "replacement");
        try
        {
            _ = overwrite.Should().Throw<IOException>();
        }
        finally
        {
            drain.SetResult();
        }

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        while (File.Exists(runner.ManifestPath))
        {
            await Task.Delay(20, timeout.Token).ConfigureAwait(false);
        }

        _ = overwrite.Should().NotThrow();
    }
}
