// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Uses installed native interpretation in graph tests instead of duplicating descriptor parsing.</summary>
internal sealed class NativeSourceFactsFixture(ProjectContext project, ICookDocumentRegistry documents) : ICookSourceFactsProvider
{
    public static async Task<Import.NativeSourceAnalysisReport> AnalyzeAsync(ContentSourceAnalysisExecution execution, CancellationToken cancellationToken)
    {
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var native = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(),
            NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var report = await native.AnalyzeSourcesAsync(execution with { Artifacts = null }, cancellationToken).ConfigureAwait(false);

        // Workflow tests control producer invalidation separately from native source interpretation.
        return report with { ProducerFingerprint = execution.Artifacts?.Fingerprint ?? report.ProducerFingerprint };
    }

    public async Task<CookSourceFrontier> ReadAsync(IReadOnlyList<ContentCookInput> inputs, CancellationToken cancellationToken)
    {
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var operation = new ContentCookOperation(Guid.NewGuid(), project, 1);
        var verified = await compatibility.VerifyAsync(operation.OperationId, cancellationToken).ConfigureAwait(false);
        var artifacts = verified.Artifacts ?? throw new InvalidOperationException("The installed test SDK is unavailable.");
        await using var lifetime = artifacts.ConfigureAwait(false);
        var native = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(),
            NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var scopes = new ProjectCookScopeProvider(new DroidNet.Storage.Native.NativeStorageProvider(new Testably.Abstractions.RealFileSystem()));
        var analyzer = new CookSourceAnalyzer(operation, artifacts, native, new ContentImportManifestBuilder(),
            new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(native)), scopes, documents);
        return await analyzer.ReadAsync(inputs, cancellationToken).ConfigureAwait(false);
    }
}
