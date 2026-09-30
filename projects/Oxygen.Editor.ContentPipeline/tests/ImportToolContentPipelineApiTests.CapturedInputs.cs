// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies logical-source identity and captured-input transport ownership.</summary>
public sealed partial class ImportToolContentPipelineApiTests
{
    /// <summary>Private bytes travel in a capture map without replacing manifest source coordinates.</summary>
    /// <returns>The asynchronous transport test.</returns>
    [TestMethod]
    public async Task CapturedImport_PreservesLogicalRootAndUsesNativeSchema()
    {
        using var workspace = new TempWorkspace();
        var execution = CapturedExecution(workspace);
        var runner = new CaptureContractRunner();
        var result = await CreateQueryApi(workspace, runner).ImportAsync(execution, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Succeeded.Should().BeTrue();
        _ = runner.Request!.Arguments.Should().ContainInOrder("--root", workspace.Root);
        var schemas = EditorSchemaCatalog.LoadFromDirectory(Path.Combine(AppContext.BaseDirectory, "Schemas"));
        var capture = JsonNode.Parse(runner.CaptureJson!)!;
        _ = schemas.ValidateAgainstEngine("oxygen.captured-inputs.schema.json", capture).Should().BeTrue();
        _ = capture["inputs"]![0]!["logical_path"]!.GetValue<string>().Should().Be(Path.Combine(workspace.Root, execution.Manifest.Jobs[0].Source));
        _ = capture["inputs"]![0]!["file"]!["path"]!.GetValue<string>().Should().StartWith(execution.OperationRoot);
        _ = capture["inputs"]![1]!["exists"]!.GetValue<bool>().Should().BeFalse();
        _ = capture["inputs"]![1]!["metadata"].Should().BeNull();
        _ = capture["inputs"]![1]!["file"].Should().BeNull();
        _ = Directory.EnumerateFiles(Path.GetDirectoryName(runner.CapturePath!)!).Should().BeEmpty();
    }

    /// <summary>The capture map remains available while a failed-to-terminate worker is alive.</summary>
    /// <returns>The asynchronous lifetime test.</returns>
    [TestMethod]
    public async Task CapturedImport_RetainsMapUntilWorkerDrain()
    {
        using var workspace = new TempWorkspace();
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var runner = new CaptureContractRunner(new ContentPipelineTerminationException(new IOException("Termination failed"), drain.Task));
        var execution = CapturedExecution(workspace);
        Func<Task> import = () => CreateQueryApi(workspace, runner).ImportAsync(execution, this.TestContext.CancellationToken);
        var failure = await import.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
        _ = File.Exists(runner.CapturePath).Should().BeTrue();
        drain.SetResult();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        await failure.Which.DrainCompletion.WaitAsync(timeout.Token).ConfigureAwait(false);
        _ = Directory.EnumerateFiles(Path.GetDirectoryName(runner.CapturePath!)!).Should().BeEmpty();
    }

    private static ContentImportExecution CapturedExecution(TempWorkspace workspace)
    {
        var execution = CreateExecution(workspace, CreateManifest(workspace));
        return execution with
        {
            CapturedInputs = new(
            [
                new(
                    Path.Combine(workspace.Root, execution.Manifest.Jobs[0].Source),
                    true,
                    new(2, false, false, 0, 0),
                    new(Path.Combine(execution.OperationRoot, "captured.json"), 2, new string('a', 64))),
                new(Path.Combine(workspace.Root, "absent.png"), false, null, null),
                new(Path.Combine(workspace.Root, "probe.png"), true, null, null),
            ]),
        };
    }

    private sealed class CaptureContractRunner(Exception? failure = null) : IContentPipelineProcessRunner
    {
        private readonly CapturingRunner importer = new(new(0, string.Empty, string.Empty));

        public ContentPipelineProcessRequest? Request { get; private set; }

        public string? CapturePath { get; private set; }

        public string? CaptureJson { get; private set; }

        public async Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
        {
            this.Request = request;
            var arguments = request.Arguments.ToList();
            this.CapturePath = arguments[arguments.IndexOf("--captured-inputs") + 1];
            this.CaptureJson = await File.ReadAllTextAsync(this.CapturePath, cancellationToken).ConfigureAwait(false);
            var result = await this.importer.RunAsync(request, cancellationToken).ConfigureAwait(false);
            return failure is { } error ? throw error : result;
        }
    }
}
