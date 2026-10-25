// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class SourceBundleDiscoveryTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Positive directory probes survive source retention and replacement staging without becoming byte-file records.</summary>
    /// <returns>The directory-retention verification.</returns>
    [TestMethod]
    public async Task DirectoryProbeSurvivesRetentionAndReplacementStaging()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("model.gltf", "source");
        var directory = Path.Combine(Path.GetDirectoryName(primary.SourcePath.FullPath)!, "empty-textures");
        _ = Directory.CreateDirectory(directory);
        var native = new DelegateSourceAnalyzer(async (execution, token) =>
        {
            var report = await SourceFactsAsync(execution, token).ConfigureAwait(false);
            return report with
            {
                Jobs = [report.Jobs.Single() with
                {
                    Observations = [.. report.Jobs.Single().Observations, new(directory, Exists: true, CookSavedSourceReader.ReadMetadata(directory), [])],
                }],
            };
        });
        var retained = await RetainDiscoveredAsync(workspace, primary.SourcePath.FullPath, native, this.TestContext.CancellationToken).ConfigureAwait(false);
        var root = Path.Combine(workspace.ProjectRoot, retained.DirectoryRelativePath);
        _ = retained.Files.Should().ContainSingle();
        _ = Directory.Exists(Path.Combine(root, "empty-textures")).Should().BeTrue();

        var stage = Path.Combine(workspace.ProjectRoot, ".build", "replacement");
        _ = await global::Oxygen.Editor.ContentPipeline.Publication.CookRootImage.CaptureAsync(root, stage, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = Directory.Exists(Path.Combine(stage, "empty-textures")).Should().BeTrue();
        _ = Directory.EnumerateFileSystemEntries(Path.Combine(stage, "empty-textures")).Should().BeEmpty();
    }

    /// <summary>Native discovery preserves embedded FBX and parent-relative glTF dependencies after original removal.</summary>
    /// <param name="extension">The retained source format.</param>
    /// <returns>The asynchronous native retention test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("gltf")]
    [DataRow("fbx")]
    public async Task NativeDiscoveryRetainsPortableSourceBundle(string extension)
    {
        using var workspace = new RetentionWorkspace();
        var original = await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle." + extension), this.TestContext.CancellationToken).ConfigureAwait(false);
        byte[]? bufferBytes = null;
        if (string.Equals(extension, "gltf", StringComparison.Ordinal))
        {
            var document = JsonNode.Parse(original)!;
            var buffer = document["buffers"]![0]!;
            var embedded = buffer["uri"]!.GetValue<string>();
            bufferBytes = Convert.FromBase64String(embedded[(embedded.IndexOf(',', StringComparison.Ordinal) + 1)..]);
            buffer["uri"] = "../Shared/mesh%20data.bin";
            original = document.ToJsonString();
        }

        var primary = workspace.Write("Models/model." + extension, original);
        var originalRoot = Path.GetDirectoryName(Path.GetDirectoryName(primary.SourcePath.FullPath))!;
        if (bufferBytes is not null)
        {
            var dependency = workspace.Write("Shared/mesh data.bin", string.Empty);
            await File.WriteAllBytesAsync(dependency.SourcePath.FullPath, bufferBytes, this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var native = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var source = await RetainDiscoveredAsync(workspace, primary.SourcePath.FullPath, native, this.TestContext.CancellationToken).ConfigureAwait(false);
        Directory.Delete(originalRoot, recursive: true);
        var bundle = Path.Combine(workspace.ProjectRoot, source.DirectoryRelativePath);
        var expectedPrimary = bufferBytes is not null ? "Models/model.gltf" : "model.fbx";
        _ = source.PrimaryRelativePath.Should().Be(expectedPrimary);
        _ = (await File.ReadAllTextAsync(Path.Combine(bundle, expectedPrimary), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(original);
        _ = source.Files.Should().HaveCount(bufferBytes is null ? 1 : 2);
        if (bufferBytes is not null)
        {
            _ = (await File.ReadAllBytesAsync(Path.Combine(bundle, "Shared/mesh data.bin"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(bufferBytes);
        }

        foreach (var file in source.Files)
        {
            _ = Convert.ToHexString(SHA256.HashData(await File.ReadAllBytesAsync(Path.Combine(bundle, file.RelativePath), this.TestContext.CancellationToken).ConfigureAwait(false))).Should().Be(file.Sha256);
        }

        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, ".cooked")).Should().BeFalse();
    }

    /// <summary>A changed primary is rediscovered, including a changed common root and primary-relative path.</summary>
    /// <returns>The asynchronous coherent retry test.</returns>
    [TestMethod]
    public async Task ChangedPrimaryRetriesDiscoveryWithItsNewBundleLayout()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("Models/model.gltf", "old source");
        _ = workspace.Write("Models/data.bin", "old buffer");
        _ = workspace.Write("Shared/data.bin", "new buffer");
        var inspector = new DelegateSourceAnalyzer(async (execution, token) =>
        {
            var observed = await File.ReadAllTextAsync(execution.Jobs.Single().AttributedSource, token).ConfigureAwait(false);
            if (string.Equals(observed, "old source", StringComparison.Ordinal))
            {
                var facts = await SourceFactsAsync(execution, token, "data.bin").ConfigureAwait(false);
                await File.WriteAllTextAsync(primary.SourcePath.FullPath, "new source", token).ConfigureAwait(false);
                return facts;
            }

            return await SourceFactsAsync(execution, token, "../Shared/data.bin").ConfigureAwait(false);
        });

        var result = await RetainDiscoveredAsync(workspace, primary.SourcePath.FullPath, inspector, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = inspector.Executions.Should().HaveCount(2);
        _ = result.PrimaryRelativePath.Should().Be("Models/model.gltf");
        _ = result.Files.Select(static file => file.RelativePath).Should().BeEquivalentTo("Models/model.gltf", "Shared/data.bin");
        _ = (await File.ReadAllTextAsync(Path.Combine(workspace.ProjectRoot, "Content/Models/Model/Models/model.gltf"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("new source");
        _ = inspector.Executions.Should().OnlyContain(execution => !Directory.Exists(execution.OperationRoot));
    }

    /// <summary>Missing dependencies are reported by original path and never expose a partial bundle.</summary>
    /// <returns>The asynchronous missing-file test.</returns>
    [TestMethod]
    public async Task MissingDependencyReportsOriginalPathWithoutPartialRetention()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("model.gltf", "source");
        var inspector = new DelegateSourceAnalyzer((execution, token) => SourceFactsAsync(execution, token, "missing.bin"));
        Func<Task> retain = async () => _ = await RetainDiscoveredAsync(workspace, primary.SourcePath.FullPath, inspector, this.TestContext.CancellationToken).ConfigureAwait(false);
        var failure = await retain.Should().ThrowAsync<CookInputDiscoveryException>().ConfigureAwait(false);
        _ = failure.Which.Diagnostics.Should().ContainSingle(issue => issue.Code == AssetImportDiagnosticCodes.SourceMissing
            && issue.AffectedPath == Path.Combine(Path.GetDirectoryName(primary.SourcePath.FullPath)!, "missing.bin"));
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "Content/Models/Model")).Should().BeFalse();
        _ = inspector.Executions.Should().OnlyContain(execution => !Directory.Exists(execution.OperationRoot));
    }

    /// <summary>Unsupported source diagnostics identify the original file, not its temporary inspection copy.</summary>
    /// <returns>The asynchronous diagnostic mapping test.</returns>
    [TestMethod]
    public async Task UnsupportedSourceReportsOriginalLocation()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("model.gltf", "source");
        var inspector = new DelegateSourceAnalyzer(async (execution, token) =>
        {
            var report = await SourceFactsAsync(execution, token).ConfigureAwait(false);
            return report with
            {
                Complete = false,
                Jobs = [report.Jobs.Single() with
                {
                    Complete = false,
                    Diagnostics = [new()
                    {
                        OperationId = Guid.Empty, Domain = FailureDomain.AssetImport, Severity = DiagnosticSeverity.Error,
                        Code = "import.unsupported", Message = "Unsupported source feature.", AffectedPath = execution.Jobs.Single().AttributedSource,
                    }
                    ],
                }],
            };
        });
        Func<Task> retain = async () => _ = await RetainDiscoveredAsync(workspace, primary.SourcePath.FullPath, inspector, this.TestContext.CancellationToken).ConfigureAwait(false);
        var failure = await retain.Should().ThrowAsync<CookInputDiscoveryException>().ConfigureAwait(false);
        _ = failure.Which.Diagnostics.Should().ContainSingle(issue => issue.AffectedPath == primary.SourcePath.FullPath && issue.OperationId != Guid.Empty);
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "Content/Models/Model")).Should().BeFalse();
    }

    /// <summary>Cancellation or project closure during native discovery cannot retain a bundle.</summary>
    /// <param name="closeProject">Whether the project closes or only the selected request is cancelled.</param>
    /// <returns>The asynchronous interrupted-query test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task InterruptedDiscoveryCleansItsQueryWithoutRetention(bool closeProject)
    {
        using var workspace = new RetentionWorkspace();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        var primary = workspace.Write("model.gltf", "source");
        var inspector = new DelegateSourceAnalyzer(async (execution, token) =>
        {
            if (closeProject)
            {
                workspace.Session.End();
            }
            else
            {
                await cancellation.CancelAsync().ConfigureAwait(false);
            }

            token.ThrowIfCancellationRequested();
            return await SourceFactsAsync(execution, token).ConfigureAwait(false);
        });
        Func<Task> retain = async () => _ = await RetainDiscoveredAsync(workspace, primary.SourcePath.FullPath, inspector, cancellation.Token).ConfigureAwait(false);
        _ = await retain.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = inspector.Executions.Should().ContainSingle();
        _ = inspector.Executions.Should().OnlyContain(execution => !Directory.Exists(execution.OperationRoot));
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "Content/Models/Model")).Should().BeFalse();
    }

    /// <summary>Failed native termination retains the query directory and operation marker until cleanup drains.</summary>
    /// <returns>The asynchronous retained-ownership test.</returns>
    [TestMethod]
    public async Task FailedTerminationRetainsNativeQueryAndOperationOwnership()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("model.gltf", "source");
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var inspector = new DelegateSourceAnalyzer((_, _) => Task.FromException<NativeSourceAnalysisReport>(new ContentPipelineTerminationException(new IOException("Worker termination failed"), drain.Task)));
        Func<Task> retain = async () => _ = await RetainDiscoveredAsync(workspace, primary.SourcePath.FullPath, inspector, this.TestContext.CancellationToken).ConfigureAwait(false);
        var failure = await retain.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
        var scratch = inspector.Executions.Single().OperationRoot;
        var marker = Path.Combine(Path.GetDirectoryName(scratch)!, "operation.lock");
        _ = Directory.Exists(scratch).Should().BeTrue();
        Action acquire = () => { using var stream = File.Open(marker, FileMode.Open, FileAccess.ReadWrite, FileShare.None); };
        try
        {
            _ = acquire.Should().Throw<IOException>();
        }
        finally
        {
            drain.SetResult();
        }

        await failure.Which.DrainCompletion.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = Directory.Exists(scratch).Should().BeFalse();
        _ = File.Exists(marker).Should().BeFalse();
        _ = await workspace.Coordinator.RunAsync((_, _) => Task.FromResult(true), this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    private static async Task<NativeSourceAnalysisReport> SourceFactsAsync(ContentSourceAnalysisExecution execution, CancellationToken token, params string[] dependencies)
    {
        var job = execution.Jobs.Single();
        var paths = dependencies.Select(path => Path.GetFullPath(Path.Combine(execution.InputRoot, path))).Prepend(job.AttributedSource).ToArray();
        var observations = ImmutableArray.CreateBuilder<NativeSourceObservation>();
        var diagnostics = ImmutableArray.CreateBuilder<DiagnosticRecord>();
        foreach (var path in paths)
        {
            if (!File.Exists(path))
            {
                observations.Add(new(path, Exists: false, Metadata: null, []));
                diagnostics.Add(new()
                {
                    OperationId = execution.OperationId,
                    Domain = FailureDomain.AssetImport,
                    Severity = DiagnosticSeverity.Error,
                    Code = AssetImportDiagnosticCodes.SourceMissing,
                    Message = "Source dependency is missing.",
                    AffectedPath = path,
                });
                continue;
            }

            var bytes = await File.ReadAllBytesAsync(path, token).ConfigureAwait(false);
            observations.Add(new(path, Exists: true, CookSavedSourceReader.ReadMetadata(path), [new(0, 0, Convert.ToHexStringLower(SHA256.HashData(bytes)))]));
        }

        return new("native-source-test", diagnostics.Count == 0, [new(
            job.Id,
            job.Type,
            job.AttributedSource,
            diagnostics.Count == 0,
            [],
            [],
            [.. paths.Select(static path => new NativeSourceFileDependency(path, Required: true))],
            observations.ToImmutable(),
            diagnostics.ToImmutable())]);
    }

    private static Task<RetainedImportSource> RetainDiscoveredAsync(RetentionWorkspace workspace, string sourcePath, IEngineContentPipelineApi native, CancellationToken cancellationToken)
    {
        var recipe = new ContentImportManifestBuilder().BuildModelJob(Path.GetFileName(sourcePath), [], "Model", AssetPath.Parse("/Content/Models/Model/" + Path.GetFileName(sourcePath)));
        var discovery = new SceneImportSourceDiscovery(workspace.Coordinator, native);
        return workspace.Coordinator.RunAsync(
            (operation, token) => workspace.Retention.RetainAsync(operation, AssetPath.Parse("/Content/Models"), "Model", captureToken => discovery.DiscoverAsync(operation, sourcePath, recipe, captureToken), token),
            cancellationToken);
    }

    private sealed class DelegateSourceAnalyzer(Func<ContentSourceAnalysisExecution, CancellationToken, Task<NativeSourceAnalysisReport>> analyze) : IEngineContentPipelineApi
    {
        public List<ContentSourceAnalysisExecution> Executions { get; } = [];

        public Task<NativeSourceAnalysisReport> AnalyzeSourcesAsync(ContentSourceAnalysisExecution execution, CancellationToken cancellationToken)
        {
            this.Executions.Add(execution);
            return analyze(execution, cancellationToken);
        }

        public Task<NativeImportResult> ImportAsync(ContentImportExecution execution, CancellationToken cancellationToken) => throw new NotSupportedException();

        public Task<global::Oxygen.Editor.ContentPipeline.Inspection.CookedInventoryReport> ReadInventoryAsync(string cookedRoot, NativeArtifactLease? artifacts, CancellationToken cancellationToken) => throw new NotSupportedException();

        public Task RemoveCookedOutputsAsync(string operationRoot, string cookedRoot, Guid sourceKey, global::Oxygen.Editor.ContentPipeline.Inspection.CookedOutputRemoval removal, NativeArtifactLease artifacts, CancellationToken cancellationToken) => throw new NotSupportedException();

        public Task<CookInspectionResult> InspectLooseCookedRootAsync(string cookedRoot, CancellationToken cancellationToken) => throw new NotSupportedException();

        public Task<CookValidationResult> ValidateLooseCookedRootAsync(string cookedRoot, CancellationToken cancellationToken) => throw new NotSupportedException();
    }
}
