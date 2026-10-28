// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed class RecordingNativeApi(ImportToolContentPipelineApi native) : IEngineContentPipelineApi, ICookedGeometryInspector
{
    public List<ContentImportExecution> Imported { get; } = [];

    public List<ContentSourceAnalysisExecution> Analyzed { get; } = [];

    public List<global::Oxygen.Editor.ContentPipeline.Inspection.CookedOutputRemoval> Removed { get; } = [];

    public bool FailNextImport { get; set; }

    public bool AddSceneWarning { get; set; }

    public Task<global::Oxygen.Editor.ContentPipeline.Import.NativeSourceAnalysisReport> AnalyzeSourcesAsync(ContentSourceAnalysisExecution execution, CancellationToken cancellationToken)
    {
        this.Analyzed.Add(execution);
        return native.AnalyzeSourcesAsync(execution, cancellationToken);
    }

    public async Task<NativeImportResult> ImportAsync(ContentImportExecution execution, CancellationToken cancellationToken)
    {
        this.Imported.Add(execution);
        if (this.FailNextImport)
        {
            this.FailNextImport = false;
            return new NativeImportResult(Succeeded: false, [new DiagnosticRecord { OperationId = execution.OperationId, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error, Code = "TEST.IMPORT_FAILED", Message = "Injected import failure." }]);
        }

        var result = await native.ImportAsync(execution, cancellationToken).ConfigureAwait(false);
        return result.Succeeded && this.AddSceneWarning
            ? result with { Diagnostics = [.. result.Diagnostics, new DiagnosticRecord { OperationId = execution.OperationId, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Warning, Code = "TEST.SCENE_WARNING", Message = "Scene warning.", AffectedVirtualPath = "/Content/Scenes/Main.oscene" }] }
            : result;
    }

    public Task<global::Oxygen.Editor.ContentPipeline.Inspection.CookedInventoryReport> ReadInventoryAsync(string root, NativeArtifactLease? artifacts, CancellationToken cancellationToken) => native.ReadInventoryAsync(root, artifacts, cancellationToken);

    public Task<global::Oxygen.Editor.ContentPipeline.Incremental.ImporterRevisions> ReadImporterRevisionsAsync(NativeArtifactLease? artifacts, CancellationToken cancellationToken) => native.ReadImporterRevisionsAsync(artifacts, cancellationToken);

    public Task RemoveCookedOutputsAsync(string operationRoot, string cookedRoot, Guid sourceKey, global::Oxygen.Editor.ContentPipeline.Inspection.CookedOutputRemoval removal, NativeArtifactLease artifacts, CancellationToken cancellationToken)
    {
        this.Removed.Add(removal);
        return native.RemoveCookedOutputsAsync(operationRoot, cookedRoot, sourceKey, removal, artifacts, cancellationToken);
    }

    public Task<CookInspectionResult> InspectLooseCookedRootAsync(string cookedRoot, CancellationToken cancellationToken) => native.InspectLooseCookedRootAsync(cookedRoot, cancellationToken);

    public Task<CookValidationResult> ValidateLooseCookedRootAsync(string cookedRoot, CancellationToken cancellationToken) => native.ValidateLooseCookedRootAsync(cookedRoot, cancellationToken);

    public Task<CookedGeometryReport> InspectGeometryAsync(string operationRoot, string cookedRoot, string virtualPath, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
        => native.InspectGeometryAsync(operationRoot, cookedRoot, virtualPath, cancellationToken, artifacts);
}
