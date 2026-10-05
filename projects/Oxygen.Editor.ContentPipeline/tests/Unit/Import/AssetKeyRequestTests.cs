// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using static Oxygen.Editor.ContentPipeline.TestSupport.ImportAdapterScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class AssetKeyRequestTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Failed termination retains both private JSON files and the native tool until drain completes.</summary>
    /// <returns>The asynchronous ownership regression.</returns>
    [TestMethod]
    public async Task AssetKeyLookupRetainsRequestAndToolUntilWorkerDrain()
    {
        using var workspace = new ImportAdapterWorkspace();
        var tool = Path.Combine(workspace.Root, "Oxygen.Cooker.Inspector.exe");
        await File.WriteAllTextAsync(tool, "Inspector", this.TestContext.CancellationToken).ConfigureAwait(false);
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var runner = new AssetKeyRunner(AssetKeyReport, new ContentPipelineTerminationException(new IOException("Simulated failure"), drain.Task));
        Func<Task> work = () => CreateQueryApi(workspace, runner).ResolveAssetKeysAsync(workspace.Root, ["/Game/Physics/Materials/Rubber.opmat"], this.TestContext.CancellationToken);
        var failure = (await work.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false)).Which;
        var request = runner.Request!;
        var input = request.Arguments[request.Arguments.ToList().IndexOf("--input") + 1];
        Action overwrite = () => File.WriteAllText(tool, "replacement");
        try
        {
            _ = File.Exists(input).Should().BeTrue();
            _ = File.Exists(runner.ReportPath).Should().BeTrue();
            _ = overwrite.Should().Throw<IOException>();
        }
        finally
        {
            drain.SetResult();
            await failure.DrainCompletion.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        _ = File.Exists(input).Should().BeFalse();
        _ = File.Exists(runner.ReportPath).Should().BeFalse();
        _ = overwrite.Should().NotThrow();
    }

    /// <summary>A well-formed report for the wrong candidates is rejected and its private files are removed.</summary>
    /// <returns>The asynchronous report-validation regression.</returns>
    [TestMethod]
    public async Task AssetKeyLookupRejectsWrongMapAndCleansRequest()
    {
        using var workspace = new ImportAdapterWorkspace();
        await File.WriteAllTextAsync(Path.Combine(workspace.Root, "Oxygen.Cooker.Inspector.exe"), "Inspector", this.TestContext.CancellationToken).ConfigureAwait(false);
        var runner = new AssetKeyRunner(AssetKeyReport);
        Func<Task> work = () => CreateQueryApi(workspace, runner).ResolveAssetKeysAsync(workspace.Root, ["/Content/Materials/Other.omat"], this.TestContext.CancellationToken);
        _ = await work.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, "dependency-inspection")).Should().BeEmpty();
    }

    private const string AssetKeyReport = """
        {"schema":"oxygen.asset-key-map.v1","assets":[{"virtual_path":"/Game/Physics/Materials/Rubber.opmat","asset_key":"5793612a-1c25-ca81-a7a2-8e696378559e"}]}
        """;
    private sealed class AssetKeyRunner(string json, Exception? failure = null) : IContentPipelineProcessRunner
    {
        public ContentPipelineProcessRequest? Request { get; private set; }

        public string? ReportPath { get; private set; }

        public async Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
        {
            this.Request = request;
            this.ReportPath = request.Arguments[request.Arguments.ToList().IndexOf("--output") + 1];
            await File.WriteAllTextAsync(this.ReportPath, json, cancellationToken).ConfigureAwait(false);
            return failure is null ? new(0, string.Empty, string.Empty) : throw failure;
        }
    }
}
