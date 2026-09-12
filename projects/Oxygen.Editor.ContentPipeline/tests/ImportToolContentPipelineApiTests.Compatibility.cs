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
    /// <summary>The deployed editor schema names and installed cooker pass the real preflight.</summary>
    /// <returns>The asynchronous installed-input check.</returns>
    [TestMethod]
    public async Task InstalledCookerInputsMatchEditorSchemas()
    {
        var result = await EditorNativeCompatibilityService.ForCooking().VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Succeeded.Should().BeTrue(string.Join("; ", result.Diagnostics.Select(static value => value.TechnicalMessage ?? value.Message)));
        await result.Artifacts!.DisposeAsync().ConfigureAwait(false);
    }

    /// <summary>Changed tool bytes block both native import and catalog discovery.</summary>
    /// <param name="catalog">Whether to exercise catalog discovery.</param>
    /// <returns>The asynchronous compatibility test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task MismatchedToolNeverLaunchesWorker(bool catalog)
    {
        using var workspace = new TempWorkspace();
        await File.WriteAllTextAsync(workspace.ToolPath, "Changed after compatibility", this.TestContext.CancellationToken).ConfigureAwait(false);
        var runner = new CapturingRunner(new(0, string.Empty, string.Empty));
        var api = new ImportToolContentPipelineApi(new FixedToolLocator(workspace.ToolPath), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Compatibility);
        if (catalog)
        {
            Func<Task> discover = () => api.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", this.TestContext.CancellationToken);
            var failure = await discover.Should().ThrowAsync<NativeCompatibilityException>().ConfigureAwait(false);
            _ = failure.Which.Diagnostics.Should().ContainSingle(item => item.Code == NativeCompatibilityDiagnosticCodes.ArtifactMismatch);
        }
        else
        {
            var result = await api.ImportAsync(CreateExecution(workspace, CreateManifest(workspace)), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.Succeeded.Should().BeFalse();
            _ = result.Diagnostics.Should().ContainSingle(item => item.Code == NativeCompatibilityDiagnosticCodes.ArtifactMismatch);
        }

        _ = runner.Request.Should().BeNull();
    }

    /// <summary>Failed termination retains compatibility leases until the worker/reader drain completes.</summary>
    /// <returns>The asynchronous worker ownership test.</returns>
    [TestMethod]
    public async Task CompatibilityLeaseSurvivesWorkerTerminationFailure()
    {
        using var workspace = new TempWorkspace();
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var runner = new FailingWorkerRunner(new ContentPipelineTerminationException(new IOException("Termination failed"), drain.Task));
        var api = new ImportToolContentPipelineApi(new FixedToolLocator(workspace.ToolPath), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Compatibility);
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
