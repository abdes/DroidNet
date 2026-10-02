// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.ImportAdapterScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class NativeCompatibilityTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Changed tool bytes block both native import and catalog discovery.</summary>
    /// <param name="catalog">Whether to exercise catalog discovery.</param>
    /// <returns>The asynchronous compatibility test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task MismatchedToolNeverLaunchesWorker(bool catalog)
    {
        using var workspace = new ImportAdapterWorkspace();
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
        using var workspace = new ImportAdapterWorkspace();
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
