// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

/// <summary>Uses installed native interpretation in graph tests instead of duplicating descriptor parsing.</summary>
internal sealed class NativeSourceFactsFixture(ProjectContext project) : ICookSourceFactsProvider
{
    /// <summary>Creates the dependency discovery of a workspace over the installed native analysis.</summary>
    /// <param name="workspace">The workspace whose sources are analyzed.</param>
    /// <returns>The discovery.</returns>
    public static CookDependencyDiscovery CreateDiscovery(CookWorkspace workspace)
        => new(new NativeSourceFactsFixture(workspace.ProjectContext));

    public static async Task<global::Oxygen.Editor.ContentPipeline.Import.NativeSourceAnalysisReport> AnalyzeAsync(ContentSourceAnalysisExecution execution, CancellationToken cancellationToken)
    {
        var compatibility = EditorNativeCompatibilityService.ForCooking();
        var native = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        return await native.AnalyzeSourcesAsync(execution with { Artifacts = null }, cancellationToken).ConfigureAwait(false);
    }

    public async Task<CookSourceFrontier> ReadAsync(IReadOnlyList<ContentCookInput> inputs, CancellationToken cancellationToken)
    {
        var compatibility = EditorNativeCompatibilityService.ForCooking();
        var operation = new ContentCookOperation(Guid.NewGuid(), project, 1);
        var verified = await compatibility.VerifyAsync(operation.OperationId, cancellationToken).ConfigureAwait(false);
        var artifacts = verified.Artifacts ?? throw new InvalidOperationException("The installed test SDK is unavailable.");
        await using var lifetime = artifacts.ConfigureAwait(false);
        var native = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var scopes = new ProjectCookScopeProvider(new DroidNet.Storage.Native.NativeStorageProvider(new Testably.Abstractions.RealFileSystem()));
        var analyzer = new CookSourceAnalyzer(operation, artifacts, native, new ContentImportManifestBuilder(),
            new SceneDescriptorGenerator(), scopes);
        return await analyzer.ReadAsync(inputs, cancellationToken).ConfigureAwait(false);
    }
}
