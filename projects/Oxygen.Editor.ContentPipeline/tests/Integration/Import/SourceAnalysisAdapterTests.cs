// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.ImportAdapterScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class SourceAnalysisAdapterTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The installed SDK analyzes and cooks the same logical sources after originals are removed.</summary>
    /// <returns>The asynchronous native contract test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task SourceAnalysis_InstalledToolCooksItsCapturedInputs()
    {
        using var workspace = new ImportAdapterWorkspace();
        await File.WriteAllTextAsync(Path.Combine(workspace.Root, "first.json"), "{\"name\":\"First\"}", this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllTextAsync(Path.Combine(workspace.Root, "second.json"), "{\"name\":\"Second\"}", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
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
}
