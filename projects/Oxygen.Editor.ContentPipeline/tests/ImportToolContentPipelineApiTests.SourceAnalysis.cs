// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Qualifies native batch analysis, diagnostic attribution and query ownership.</summary>
public sealed partial class ImportToolContentPipelineApiTests
{
    /// <summary>A frontier uses one destination-free manifest and binds facts to the verified producer.</summary>
    /// <returns>The asynchronous query test.</returns>
    [TestMethod]
    public async Task SourceAnalysis_BatchesJobsAndUsesArtifactFingerprint()
    {
        using var workspace = new TempWorkspace();
        var runner = new SourceAnalysisRunner();
        var execution = AnalysisExecution(workspace);
        var result = await AnalysisApi(workspace, runner).AnalyzeSourcesAsync(execution, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Complete.Should().BeTrue();
        _ = result.ProducerFingerprint.Should().NotBe("display-only-version");
        _ = result.Jobs.Should().HaveCount(2);
        _ = runner.Calls.Should().Be(1);
        _ = runner.Request!.Arguments.Should().ContainInOrder("analyze-sources", "--manifest", runner.ManifestPath!, "--root", execution.InputRoot, "--report", runner.ReportPath!);
        using var manifest = JsonDocument.Parse(runner.ManifestJson!);
        _ = manifest.RootElement.TryGetProperty("output", out _).Should().BeFalse();
        _ = manifest.RootElement.GetProperty("jobs")[0].TryGetProperty("name", out _).Should().BeFalse();
        _ = File.Exists(runner.ManifestPath).Should().BeFalse();
        _ = File.Exists(runner.ReportPath).Should().BeFalse();
        var observation = result.Jobs[0].Observations[0];
        _ = observation.Reads.Should().ContainSingle();
        _ = observation.Reads[0].Offset.Should().Be(2);
        _ = observation.Reads[0].MaxBytes.Should().Be(4);
        _ = observation.Metadata!.LastModifiedNanoseconds.Should().Be(123456700);
    }

    /// <summary>A failing native analysis still returns its attributed report.</summary>
    /// <returns>The asynchronous diagnostic test.</returns>
    [TestMethod]
    public async Task SourceAnalysis_PreservesReportOnNonzeroExit()
    {
        using var workspace = new TempWorkspace();
        var runner = new SourceAnalysisRunner { ExitCode = 1 };
        var execution = AnalysisExecution(workspace);
        var report = await AnalysisApi(workspace, runner).AnalyzeSourcesAsync(execution, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = report.Complete.Should().BeFalse();
        _ = report.Jobs[0].Diagnostics.Should().ContainSingle(issue => issue.OperationId == execution.OperationId
            && issue.Code == "analysis.source_missing" && issue.Severity == DiagnosticSeverity.Error
            && issue.AffectedPath == Path.Combine(workspace.Root, "first.json") && issue.TechnicalMessage == "textures.base_color");
    }

    /// <summary>A query cannot silently associate another frontier's report with the caller.</summary>
    /// <returns>The asynchronous correlation test.</returns>
    [TestMethod]
    public async Task SourceAnalysis_RejectsMismatchedJobIdentity()
    {
        using var workspace = new TempWorkspace();
        var runner = new SourceAnalysisRunner { ChangeJobIdentity = true };
        Func<Task> analyze = () => AnalysisApi(workspace, runner).AnalyzeSourcesAsync(AnalysisExecution(workspace), this.TestContext.CancellationToken);
        _ = await analyze.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        _ = File.Exists(runner.ReportPath).Should().BeFalse();
    }

    /// <summary>Malformed or contradictory facts cannot enter the capture graph.</summary>
    /// <param name="failure">The malformed native field.</param>
    /// <returns>The asynchronous validation test.</returns>
    [TestMethod]
    [DataRow("version")]
    [DataRow("path")]
    [DataRow("absence")]
    [DataRow("completion")]
    [DataRow("partial-error")]
    [DataRow("source")]
    public async Task SourceAnalysis_RejectsInvalidFacts(string failure)
    {
        using var workspace = new TempWorkspace();
        var runner = new SourceAnalysisRunner
        {
            RewriteReport = report =>
            {
                var job = report["jobs"]![0]!;
                switch (failure)
                {
                    case "version": report["version"] = 2; break;
                    case "path": job["observations"]![0]!["path"] = "relative.json"; break;
                    case "absence": job["observations"]![0]!["exists"] = false; break;
                    case "completion": job["complete"] = false; break;
                    case "source": job["source_path"] = Path.Combine(workspace.Root, "another-source.json"); break;
                    default:
                        report["complete"] = false;
                        job["diagnostics"] = new JsonArray(new JsonObject
                        {
                            ["severity"] = "Error", ["code"] = "analysis.failed", ["message"] = "Source failed.",
                            ["source_path"] = job["source_path"]!.GetValue<string>(), ["object_path"] = string.Empty,
                        });
                        break;
                }
            },
        };
        Func<Task> analyze = () => AnalysisApi(workspace, runner).AnalyzeSourcesAsync(AnalysisExecution(workspace), this.TestContext.CancellationToken);
        _ = await analyze.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        _ = File.Exists(runner.ReportPath).Should().BeFalse();
    }

    /// <summary>Termination failure retains both query files and owned artifacts until drain.</summary>
    /// <returns>The asynchronous lifetime test.</returns>
    [TestMethod]
    public async Task SourceAnalysis_RetainsInputsUntilWorkerDrain()
    {
        using var workspace = new TempWorkspace();
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var runner = new SourceAnalysisRunner { Failure = new ContentPipelineTerminationException(new IOException("Termination failed"), drain.Task) };
        var execution = AnalysisExecution(workspace) with { CapturedInputs = new([]) };
        Func<Task> analyze = () => AnalysisApi(workspace, runner).AnalyzeSourcesAsync(execution, this.TestContext.CancellationToken);
        var failure = await analyze.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
        _ = File.Exists(runner.ManifestPath).Should().BeTrue();
        _ = File.Exists(runner.ReportPath).Should().BeTrue();
        _ = File.Exists(runner.CapturePath).Should().BeTrue();
        try
        {
            Action overwrite = () => File.WriteAllText(workspace.ToolPath, "replacement");
            _ = overwrite.Should().Throw<IOException>();
        }
        finally
        {
            drain.SetResult();
        }

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        await failure.Which.DrainCompletion.WaitAsync(timeout.Token).ConfigureAwait(false);
        _ = File.Exists(runner.ManifestPath).Should().BeFalse();
        _ = File.Exists(runner.ReportPath).Should().BeFalse();
        _ = File.Exists(runner.CapturePath).Should().BeFalse();
        Action replace = () => File.WriteAllText(workspace.ToolPath, "replacement");
        _ = replace.Should().NotThrow();
    }

    /// <summary>A nested query does not dispose the enclosing cook's artifact lease.</summary>
    /// <returns>The asynchronous borrowed-lease test.</returns>
    [TestMethod]
    public async Task SourceAnalysis_PreservesBorrowedArtifacts()
    {
        using var workspace = new TempWorkspace();
        var compatibility = await workspace.Compatibility.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var artifacts = compatibility.Artifacts!;
        await using var lifetime = artifacts.ConfigureAwait(false);
        var report = await AnalysisApi(workspace, new SourceAnalysisRunner()).AnalyzeSourcesAsync(
            AnalysisExecution(workspace) with { Artifacts = artifacts }, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = report.ProducerFingerprint.Should().Be(artifacts.Fingerprint);
        Action overwrite = () => File.WriteAllText(workspace.ToolPath, "replacement");
        _ = overwrite.Should().Throw<IOException>();
    }

    /// <summary>A new accepted producer uses its own schema rather than a process-lifetime copy.</summary>
    /// <returns>The asynchronous schema-binding test.</returns>
    [TestMethod]
    public async Task SourceAnalysis_UsesSchemaFromCurrentArtifactLease()
    {
        using var workspace = new TempWorkspace();
        var schemaPath = Path.Combine(AppContext.BaseDirectory, "Schemas", "oxygen.source-analysis.schema.json");
        var changed = JsonNode.Parse(await File.ReadAllTextAsync(schemaPath, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        changed["properties"]!["complete"]!["const"] = false;
        var changedPath = Path.Combine(workspace.Root, "changed-schema.json");
        await File.WriteAllTextAsync(changedPath, changed.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var firstProducer = AnalysisArtifacts(workspace, schemaPath);
        using var secondProducer = AnalysisArtifacts(workspace, changedPath);
        var first = (await firstProducer.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false)).Artifacts!;
        var second = (await secondProducer.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false)).Artifacts!;
        await using var firstLifetime = first.ConfigureAwait(false);
        await using var secondLifetime = second.ConfigureAwait(false);
        var api = AnalysisApi(workspace, new SourceAnalysisRunner());
        _ = first.Fingerprint.Should().NotBe(second.Fingerprint);
        _ = (await api.AnalyzeSourcesAsync(AnalysisExecution(workspace) with { Artifacts = first }, this.TestContext.CancellationToken).ConfigureAwait(false)).Complete.Should().BeTrue();
        Func<Task> analyze = () => api.AnalyzeSourcesAsync(AnalysisExecution(workspace) with { Artifacts = second }, this.TestContext.CancellationToken);
        _ = await analyze.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
    }

    /// <summary>The installed SDK analyzes and cooks the same logical sources after originals are removed.</summary>
    /// <returns>The asynchronous native contract test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task SourceAnalysis_InstalledToolCooksItsCapturedInputs()
    {
        using var workspace = new TempWorkspace();
        await File.WriteAllTextAsync(Path.Combine(workspace.Root, "first.json"), "{\"name\":\"First\"}", this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllTextAsync(Path.Combine(workspace.Root, "second.json"), "{\"name\":\"Second\"}", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var verified = await compatibility.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var artifacts = verified.Artifacts!;
        await using var lifetime = artifacts.ConfigureAwait(false);
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var execution = AnalysisExecution(workspace) with { Artifacts = artifacts };
        var analysis = await api.AnalyzeSourcesAsync(execution, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = analysis.Complete.Should().BeTrue();
        _ = analysis.ProducerFingerprint.Should().Be(artifacts.Fingerprint);
        var captures = await CaptureAnalyzedSourcesAsync(execution, analysis, this.TestContext.CancellationToken).ConfigureAwait(false);
        var capturedAnalysis = await api.AnalyzeSourcesAsync(execution with { CapturedInputs = captures }, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = capturedAnalysis.Should().BeEquivalentTo(analysis);
        var manifest = new ContentImportManifest(1, Path.Combine(execution.OperationRoot, "output"), new("/.cooked"), execution.Jobs);
        var import = new ContentImportExecution(execution.OperationId, execution.InputRoot, execution.OperationRoot, manifest)
        {
            Artifacts = artifacts,
            CapturedInputs = captures,
        };
        var imported = await api.ImportAsync(import, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = imported.Succeeded.Should().BeTrue();
        _ = imported.OutputsBySource.Should().HaveCount(2);
        foreach (var output in analysis.Jobs.SelectMany(static job => job.Outputs))
        {
            _ = File.Exists(Path.Combine(manifest.Output, output.VirtualPath["/.cooked/".Length..])).Should().BeTrue();
        }
    }

    private static async Task<NativeCapturedInputSet> CaptureAnalyzedSourcesAsync(
        ContentSourceAnalysisExecution execution, NativeSourceAnalysisReport analysis, CancellationToken cancellationToken)
    {
        var directory = Path.Combine(execution.OperationRoot, "inputs");
        _ = Directory.CreateDirectory(directory);
        var inputs = new List<NativeCapturedInput>();
        foreach (var job in analysis.Jobs)
        {
            var source = job.Observations.Single();
            var bytes = await File.ReadAllBytesAsync(source.Path, cancellationToken).ConfigureAwait(false);
            var digest = Convert.ToHexStringLower(SHA256.HashData(bytes));
            _ = source.Reads.Should().ContainSingle(read => read.Offset == 0 && read.MaxBytes == 0 && string.Equals(read.Sha256, digest, StringComparison.Ordinal));
            var captured = Path.Combine(directory, job.Id + ".capture");
            await File.WriteAllBytesAsync(captured, bytes, cancellationToken).ConfigureAwait(false);
            inputs.Add(new(source.Path, true, source.Metadata, new(captured, (ulong)bytes.LongLength, digest)));
            _ = source.Path.Should().Be(Path.Combine(execution.InputRoot, job.Id + ".json"));
            File.Delete(source.Path);
        }

        return new([.. inputs]);
    }

    private static ContentSourceAnalysisExecution AnalysisExecution(TempWorkspace workspace) => new(
        Guid.NewGuid(),
        workspace.Root,
        Path.Combine(workspace.Root, "operation"),
        [new("first", "material-descriptor", "first.json", [], null, null), new("second", "material-descriptor", "second.json", [], null, null)]);

    private static ImportToolContentPipelineApi AnalysisApi(TempWorkspace workspace, IContentPipelineProcessRunner runner)
        => new(new FixedToolLocator(workspace.ToolPath), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Compatibility);

    private static Oxygen.Testing.TemporaryNativeArtifacts AnalysisArtifacts(TempWorkspace workspace, string schemaPath) => new(
    [
        new(NativeArtifactInventory.ImportToolId, workspace.ToolPath),
        new(NativeArtifactInventory.SourceAnalysisSchemaId, schemaPath),
        new(NativeArtifactInventory.CapturedInputsSchemaId, Path.Combine(AppContext.BaseDirectory, "Schemas", "oxygen.captured-inputs.schema.json")),
    ]);

    private sealed class SourceAnalysisRunner : IContentPipelineProcessRunner
    {
        public int Calls { get; private set; }

        public int ExitCode { get; init; }

        public bool ChangeJobIdentity { get; init; }

        public Exception? Failure { get; init; }

        public Action<JsonObject>? RewriteReport { get; init; }

        public ContentPipelineProcessRequest? Request { get; private set; }

        public string? ManifestPath { get; private set; }

        public string? ReportPath { get; private set; }

        public string? CapturePath { get; private set; }

        public string? ManifestJson { get; private set; }

        public async Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
        {
            this.Calls++;
            this.Request = request;
            var arguments = request.Arguments.ToList();
            this.ManifestPath = arguments[arguments.IndexOf("--manifest") + 1];
            this.ReportPath = arguments[arguments.IndexOf("--report") + 1];
            this.CapturePath = arguments.Contains("--captured-inputs", StringComparer.Ordinal)
                ? arguments[arguments.IndexOf("--captured-inputs") + 1] : null;
            var root = arguments[arguments.IndexOf("--root") + 1];
            this.ManifestJson = await File.ReadAllTextAsync(this.ManifestPath, cancellationToken).ConfigureAwait(false);
            using var manifest = JsonDocument.Parse(this.ManifestJson);
            var jobs = new JsonArray();
            foreach (var job in manifest.RootElement.GetProperty("jobs").EnumerateArray())
            {
                var source = Path.GetFullPath(job.GetProperty("source").GetString()!, root);
                jobs.Add(new JsonObject
                {
                    ["id"] = this.ChangeJobIdentity ? "different" : job.GetProperty("id").GetString(),
                    ["job_type"] = job.GetProperty("type").GetString(), ["source_path"] = source,
                    ["complete"] = this.ExitCode == 0, ["outputs"] = new JsonArray(), ["references"] = new JsonArray(),
                    ["files"] = new JsonArray(new JsonObject { ["path"] = source, ["required"] = true }),
                    ["accessed_paths"] = new JsonArray(JsonValue.Create(source)),
                    ["observations"] = new JsonArray(new JsonObject
                    {
                        ["path"] = source, ["exists"] = true,
                        ["metadata"] = new JsonObject
                        {
                            ["size"] = 8, ["is_directory"] = false, ["is_symlink"] = false,
                            ["last_modified_seconds"] = -1, ["last_modified_nanoseconds"] = 123456700,
                        },
                        ["reads"] = new JsonArray(new JsonObject { ["offset"] = 2, ["max_bytes"] = 4, ["sha256"] = new string('a', 64) }),
                    }),
                    ["diagnostics"] = this.ExitCode == 0 ? new JsonArray() : new JsonArray(new JsonObject
                    {
                        ["severity"] = "Error", ["code"] = "analysis.source_missing", ["message"] = "A source is missing.",
                        ["source_path"] = source, ["object_path"] = "textures.base_color",
                    }),
                });
            }

            var report = new JsonObject { ["version"] = 1, ["producer_version"] = "display-only-version", ["complete"] = this.ExitCode == 0, ["jobs"] = jobs };
            this.RewriteReport?.Invoke(report);
            await File.WriteAllTextAsync(this.ReportPath, report.ToJsonString(), cancellationToken).ConfigureAwait(false);
            return this.Failure is { } failure ? throw failure : new(this.ExitCode, string.Empty, string.Empty);
        }
    }
}
