// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Models;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Cooks retained models through the same snapshot, staging and publication path as authored descriptors.</summary>
public sealed partial class ContentPipelineService
{
    private static async Task<ImmutableArray<CookProducedSourceFile>> PrepareProducedSourceFilesAsync(
        Snapshots.CookInputSnapshot snapshot, Snapshots.CookDependencyGraph graph, ContentCookResult result, CancellationToken cancellationToken)
    {
        var outputs = ImmutableArray.CreateBuilder<CookProducedSourceFile>();
        var sources = graph.Assets.Where(static input => input.Kind == ContentCookAssetKind.ForeignSource)
            .ToDictionary(static input => input.SourceRelativePath, StringComparer.Ordinal);
        foreach (var (sourcePath, update) in result.ModelImports)
        {
            if (!sources.TryGetValue(sourcePath, out var source))
            {
                throw new InvalidDataException("A model import names a source outside this cook.");
            }

            var record = ModelRecordLookup.Find(snapshot.Operation.Project, source.SourceAbsolutePath);
            var relative = ModelRecordLookup.RelativePath(snapshot.Operation.Project, record);
            var input = snapshot.Inputs.Single(file => string.Equals(file.RelativePath, relative, StringComparison.Ordinal));
            var before = await File.ReadAllBytesAsync(Path.Combine(snapshot.InputRoot, relative), cancellationToken).ConfigureAwait(false);
            if (input.IsAbsent || !string.Equals(Convert.ToHexString(SHA256.HashData(before)), input.DiscoveryHash, StringComparison.Ordinal))
            {
                throw new InvalidDataException("The captured model import record changed during native cooking.");
            }

            var after = ModelImportSettings.Parse(record.RecordPath, before).WithImportUpdate(update).ToBytes();
            if (!before.AsSpan().SequenceEqual(after))
            {
                outputs.Add(new(relative, before, after));
            }
        }

        return outputs.ToImmutable();
    }

    private static DiagnosticRecord[] ChangedImportedSourceDiagnostics(Guid operationId, Snapshots.CookDependencyGraph graph, Incremental.CookProvenance previous, Incremental.CookIncrementalPlan plan)
    {
        var prior = previous.Products.Where(static product => product.ImportedSource is not null).ToDictionary(static product => product.SourceUri);
        return [.. graph.ImportedSources.Where(pair => !plan.Reusable.ContainsKey(pair.Key) && prior.TryGetValue(pair.Key, out var published)
                && (published.ImportedSource!.ContentFingerprint is null || !string.Equals(published.ImportedSource.ContentFingerprint, pair.Value.ContentFingerprint, StringComparison.Ordinal)))
            .Select(pair => new DiagnosticRecord
            {
                OperationId = operationId,
                Domain = FailureDomain.AssetImport,
                Severity = DiagnosticSeverity.Error,
                Code = "asset_import.reimport_required",
                AffectedVirtualPath = pair.Key.VirtualPath,
                Message = $"Reimport '{Path.GetFileName(pair.Key.VirtualPath)}' to confirm its source changes before automatic cooking updates it.",
            })];
    }

    private static DiagnosticRecord[] MissingImportedOutputs(Guid operationId, Snapshots.CookDependencyGraph graph, IEnumerable<ContentCookedAsset> outputs)
    {
        var available = outputs.Select(static output => output.CookedAssetUri).ToHashSet();
        return [.. graph.ImportedReferences.Where(uri => !available.Contains(uri)).Select(uri => new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = AssetImportDiagnosticCodes.ImportFailed,
            AffectedVirtualPath = uri.VirtualPath,
            Message = $"The retained model does not produce '{uri}'. Choose an existing imported output or correct the source before reimporting.",
        })];
    }

    private static ContentCookResult ValidateImportedIdentities(Guid operationId, ContentCookScope scope, ContentCookInput source, ContentCookResult result)
    {
        if (result.Status is not (OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings))
        {
            return result;
        }

        // A reimport keeps every product the model produced before, named under the same identity.
        var previous = scope.PreviousProvenance?.Products.FirstOrDefault(product => product.SourceUri == source.AssetUri);
        var kept = previous?.Outputs.ToArray() ?? [];
        var produced = result.CookedAssets.Where(asset => asset.SourceAssetUri == source.AssetUri)
            .Select(static asset => asset.VirtualPath).ToHashSet(StringComparer.Ordinal);
        var missing = kept.FirstOrDefault(output => !produced.Contains(output.Asset.VirtualPath));
        if (produced.Count == 0 || missing is not null)
        {
            var message = missing is not null
                ? $"Reimport would remove or rename '{missing.Asset.VirtualPath}'. Keep existing asset identities or import into a new destination."
                : "The model import produced no assets.";
            return IdentityFailure(operationId, source, result, message);
        }

        if (previous is not null && scope.PreviousInventories.TryGetValue(source.MountName, out var previousInventory))
        {
            var oldEntries = previousInventory.Assets.ToDictionary(static asset => asset.VirtualPath, StringComparer.Ordinal);
            var newEntries = result.Inspection!.Assets.ToDictionary(static asset => asset.VirtualPath, StringComparer.Ordinal);
            foreach (var output in kept)
            {
                if (oldEntries.TryGetValue(output.Asset.VirtualPath, out var old)
                    && !string.Equals(old.Key.ToString(), newEntries[output.Asset.VirtualPath].AssetKey, StringComparison.Ordinal))
                {
                    return IdentityFailure(operationId, source, result, $"Reimport would change the native identity of '{output.Asset.VirtualPath}'. Import into a new destination.");
                }
            }
        }

        return result;
    }

    private static ContentCookResult IdentityFailure(Guid operationId, ContentCookInput source, ContentCookResult result, string message)
        => result with
        {
            Status = OperationStatus.Failed,
            Diagnostics =
            [
                .. result.Diagnostics,
                new DiagnosticRecord
                {
                    OperationId = operationId,
                    Domain = FailureDomain.AssetImport,
                    Severity = DiagnosticSeverity.Error,
                    Code = AssetImportDiagnosticCodes.ImportFailed,
                    AffectedVirtualPath = source.AssetUri.VirtualPath,
                    Message = message,
                },
            ],
        };

    // The incoming file has no record yet: its files are discovered as a new model's are.
    private Task<ImportSourceBundle> DiscoverChangedImportedSourceAsync(
        ContentCookOperation operation,
        ContentCookInput input,
        NativeArtifactLease? artifacts,
        CancellationToken cancellationToken)
    {
        this.cookCoordinator.VerifyWriter(operation);
        var file = input.SourceAbsolutePath.Name;
        var recipe = this.manifestBuilder.BuildModelJob(file, [], Path.GetFileNameWithoutExtension(file), input.AssetUri);
        return new SceneImportSourceDiscovery(this.cookCoordinator, this.engineContentPipelineApi)
            .DiscoverAsync(operation, input.SourceAbsolutePath.FullPath, recipe, cancellationToken, artifacts);
    }

    private async Task<ContentCookResult> CookWithImportedSourcesAsync(Guid operationId, ContentCookScope scope, CancellationToken cancellationToken)
    {
        // The scope's models cook as one native batch, so the ImportTool runs them as concurrent jobs under the run's
        // settings; each job keeps its own layout, and each model's identities are checked against its own products.
        var results = new List<ContentCookResult>();
        var models = scope.Inputs.Where(static input => input.Kind == ContentCookAssetKind.ForeignSource).ToArray();
        if (models.Length != 0)
        {
            var jobs = models.Select(input => scope.NativeJobs.GetValueOrDefault(input.AssetUri)
                ?? this.manifestBuilder.BuildJob(input, [], ModelRecordLookup.RelativePath(scope.Project, ModelRecordLookup.Find(scope.Project, input.SourceAbsolutePath)))).ToArray();
            var manifest = new ContentImportManifest(
                1,
                (scope.Output ?? throw new InvalidOperationException("The import has no generation owner.")).Path,
                jobs[0].Layout ?? new ContentImportLayout(AssetPath.Create(models[0].AssetUri.Mount).VirtualPath),
                jobs);
            var result = await this.ExecuteManifestAsync(operationId, scope.TargetKind, scope with { Inputs = models }, manifest, [], cancellationToken).ConfigureAwait(false);
            results.Add(models.Aggregate(result, (checkedResult, input) => ValidateImportedIdentities(operationId, scope, input, checkedResult)));
        }

        var authored = scope.Inputs.Where(static input => input.Kind != ContentCookAssetKind.ForeignSource).ToArray();
        if (authored.Length != 0)
        {
            results.Add(await this.CookMixedInputsAsync(operationId, scope with { Inputs = authored }, cancellationToken).ConfigureAwait(false));
        }

        var merged = MergeProjectResults(operationId, results) with { TargetKind = scope.TargetKind };
        return merged with { VerifiedRoot = results[^1].VerifiedRoot, NativeInventory = results[^1].NativeInventory, Inspection = results[^1].Inspection, Validation = results[^1].Validation };
    }
}
