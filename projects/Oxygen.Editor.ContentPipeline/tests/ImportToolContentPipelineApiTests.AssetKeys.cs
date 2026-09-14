// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks native key-map requests and owned cleanup.</summary>
public sealed partial class ImportToolContentPipelineApiTests
{
    private const string AssetKeyReport = """
        {"schema":"oxygen.asset-key-map.v1","assets":[{"virtual_path":"/Game/Physics/Materials/Rubber.opmat","asset_key":"5793612a-1c25-ca81-a7a2-8e696378559e"}]}
        """;

    /// <summary>Failed termination retains both private JSON files and the native tool until drain completes.</summary>
    /// <returns>The asynchronous ownership regression.</returns>
    [TestMethod]
    public async Task AssetKeyLookupRetainsRequestAndToolUntilWorkerDrain()
    {
        using var workspace = new TempWorkspace();
        var tool = Path.Combine(workspace.Root, "Oxygen.Cooker.Inspector.exe");
        await File.WriteAllTextAsync(tool, "Inspector", this.TestContext.CancellationToken).ConfigureAwait(false);
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var runner = new SourceInspectionRunner(AssetKeyReport, new ContentPipelineTerminationException(new IOException("Simulated failure"), drain.Task));
        Func<Task> work = () => CreateSourceInspectionApi(workspace, runner).ResolveAssetKeysAsync(workspace.Root, ["/Game/Physics/Materials/Rubber.opmat"], this.TestContext.CancellationToken);
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
        using var workspace = new TempWorkspace();
        await File.WriteAllTextAsync(Path.Combine(workspace.Root, "Oxygen.Cooker.Inspector.exe"), "Inspector", this.TestContext.CancellationToken).ConfigureAwait(false);
        var runner = new SourceInspectionRunner(AssetKeyReport);
        Func<Task> work = () => CreateSourceInspectionApi(workspace, runner).ResolveAssetKeysAsync(workspace.Root, ["/Content/Materials/Other.omat"], this.TestContext.CancellationToken);
        _ = await work.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, "dependency-inspection")).Should().BeEmpty();
    }
}
