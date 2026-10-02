// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Publication;
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
        foreach (var (sourcePath, provenance) in result.MaterialSlotProvenance)
        {
            if (!sources.TryGetValue(sourcePath, out var source))
            {
                throw new InvalidDataException("Native material-slot provenance names a source outside this cook.");
            }

            var relative = source.SourceRelativePath + NativeSceneImportSettings.SidecarSuffix;
            var input = snapshot.Inputs.Single(file => string.Equals(file.RelativePath, relative, StringComparison.Ordinal));
            var before = await File.ReadAllBytesAsync(Path.Combine(snapshot.InputRoot, relative), cancellationToken).ConfigureAwait(false);
            if (input.IsAbsent || !string.Equals(Convert.ToHexString(SHA256.HashData(before)), input.DiscoveryHash, StringComparison.Ordinal))
            {
                throw new InvalidDataException("Captured source settings changed during native cooking.");
            }

            var settings = NativeSceneImportSettings.Parse(before);
            if (settings.MaterialSlotProvenance.SourceIdentity != provenance.SourceIdentity)
            {
                throw new InvalidDataException("Native material-slot provenance changed the retained source identity.");
            }

            var after = (settings with { MaterialSlotProvenance = provenance }).ToBytes();
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
        return graph.ImportedSources.Where(pair => !plan.Reusable.ContainsKey(pair.Key) && prior.TryGetValue(pair.Key, out var published)
                && (published.ImportedSource!.ContentFingerprint is null || !string.Equals(published.ImportedSource.ContentFingerprint, pair.Value.ContentFingerprint, StringComparison.Ordinal)))
            .Select(pair => new DiagnosticRecord
            {
                OperationId = operationId,
                Domain = FailureDomain.AssetImport,
                Severity = DiagnosticSeverity.Error,
                Code = "asset_import.reimport_required",
                AffectedVirtualPath = pair.Key.AbsolutePath,
                Message = $"Reimport '{Path.GetFileName(Uri.UnescapeDataString(pair.Key.AbsolutePath))}' to confirm its source changes before automatic cooking updates it.",
            }).ToArray();
    }

    private static DiagnosticRecord[] MissingImportedOutputs(Guid operationId, Snapshots.CookDependencyGraph graph, IEnumerable<ContentCookedAsset> outputs)
    {
        var available = outputs.Select(static output => output.CookedAssetUri).ToHashSet();
        return graph.ImportedReferences.Where(uri => !available.Contains(uri)).Select(uri => new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = AssetImportDiagnosticCodes.ImportFailed,
            AffectedVirtualPath = uri.AbsolutePath,
            Message = $"The retained model does not produce '{uri}'. Choose an existing imported output or correct the source before reimporting.",
        }).ToArray();
    }

    private static ContentCookResult ValidateImportedIdentities(Guid operationId, ContentCookScope scope, ContentCookInput source, ContentCookResult result)
    {
        if (result.Status is not (OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings))
        {
            return result;
        }

        var previous = scope.PreviousProvenance?.Products.FirstOrDefault(product => product.SourceUri == source.AssetUri);
        var produced = result.CookedAssets.Select(static asset => asset.VirtualPath).ToHashSet(StringComparer.Ordinal);
        var missing = previous?.Outputs.FirstOrDefault(output => !produced.Contains(output.Asset.VirtualPath));
        if (result.CookedAssets.Count == 0 || missing is not null)
        {
            var message = missing is not null
                ? $"Reimport would remove or rename '{missing.Asset.VirtualPath}'. Keep existing asset identities or import into a new destination."
                : "The model import produced no assets in its declared destination.";
            return IdentityFailure(operationId, source, result, message);
        }

        if (previous is not null && scope.PreviousInventories.TryGetValue(source.MountName, out var previousInventory))
        {
            var oldEntries = previousInventory.Assets.ToDictionary(static asset => asset.VirtualPath, StringComparer.Ordinal);
            var newEntries = result.Inspection!.Assets.ToDictionary(static asset => asset.VirtualPath, StringComparer.Ordinal);
            foreach (var output in previous.Outputs)
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
                    AffectedVirtualPath = source.AssetUri.AbsolutePath,
                    Message = message,
                },
            ],
        };

    private Task<ImportSourceBundle> DiscoverChangedImportedSourceAsync(ContentCookOperation operation, ContentCookInput input,
        NativeSceneImportSettings settings, NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        this.cookCoordinator.VerifyWriter(operation);
        var recipe = this.manifestBuilder.BuildJob(input, [], settings);
        return new SceneImportSourceDiscovery(cookDocuments, this.cookCoordinator, this.engineContentPipelineApi)
            .DiscoverAsync(operation, input.SourceAbsolutePath, recipe, cancellationToken, artifacts);
    }

    private async Task<ContentCookResult> CookWithImportedSourcesAsync(Guid operationId, ContentCookScope scope, CancellationToken cancellationToken)
    {
        var results = new List<ContentCookResult>();
        foreach (var input in scope.Inputs.Where(static input => input.Kind == ContentCookAssetKind.ForeignSource))
        {
            var settingsPath = Path.Combine(scope.Snapshot!.InputRoot, input.SourceRelativePath + NativeSceneImportSettings.SidecarSuffix);
            var settings = NativeSceneImportSettings.Parse(await File.ReadAllBytesAsync(settingsPath, cancellationToken).ConfigureAwait(false));
            var collision = await this.FindImportedOutputCollisionAsync(operationId, scope, input, settings, cancellationToken).ConfigureAwait(false);
            if (collision is not null)
            {
                results.Add(new(operationId, scope.TargetKind, OperationStatus.Failed, [collision], [], null, null));
                continue;
            }

            var sourceScope = scope with { Inputs = [input] };
            var manifest = new ContentImportManifest(
                1,
                (scope.Output ?? throw new InvalidOperationException("The import has no generation owner.")).Path,
                settings.CreateLayout(),
                [scope.NativeJobs.GetValueOrDefault(input.AssetUri) ?? this.manifestBuilder.BuildJob(input, [], settings)]);
            var result = await this.ExecuteManifestAsync(operationId, scope.TargetKind, sourceScope, manifest, [], cancellationToken).ConfigureAwait(false);
            results.Add(ValidateImportedIdentities(operationId, scope, input, result));
        }

        var authored = scope.Inputs.Where(static input => input.Kind != ContentCookAssetKind.ForeignSource).ToArray();
        if (authored.Length != 0)
        {
            results.Add(await this.CookMixedInputsAsync(operationId, scope with { Inputs = authored }, cancellationToken).ConfigureAwait(false));
        }

        var merged = MergeProjectResults(operationId, results) with { TargetKind = scope.TargetKind };
        return merged with { VerifiedRoot = results[^1].VerifiedRoot, NativeInventory = results[^1].NativeInventory, Inspection = results[^1].Inspection, Validation = results[^1].Validation };
    }

    private async Task<DiagnosticRecord?> FindImportedOutputCollisionAsync(Guid operationId, ContentCookScope scope, ContentCookInput source, NativeSceneImportSettings settings, CancellationToken cancellationToken)
    {
        var prefixes = settings.OutputPrefixes;
        var conflictingInput = scope.Inputs.FirstOrDefault(input => input.AssetUri != source.AssetUri
            && input.OutputVirtualPath is { } output && prefixes.Any(prefix => output.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)));
        if (conflictingInput is not null)
        {
            return Collision(conflictingInput.AssetUri.ToString());
        }

        foreach (var other in scope.Inputs.Where(input => input.Kind == ContentCookAssetKind.ForeignSource && input.AssetUri != source.AssetUri))
        {
            var otherSettingsPath = Path.Combine(scope.Snapshot!.InputRoot, other.SourceRelativePath + NativeSceneImportSettings.SidecarSuffix);
            var otherSettings = NativeSceneImportSettings.Parse(await File.ReadAllBytesAsync(otherSettingsPath, cancellationToken).ConfigureAwait(false));
            if (prefixes.Any(prefix => otherSettings.OutputPrefixes.Any(otherPrefix => prefix.StartsWith(otherPrefix, StringComparison.OrdinalIgnoreCase)
                || otherPrefix.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))))
            {
                return Collision(other.AssetUri.ToString());
            }
        }

        if (!File.Exists(Path.Combine((scope.Output ?? throw new InvalidOperationException("The import has no generation owner.")).Path, "container.index.bin")))
        {
            return null;
        }

        var inspection = await this.engineContentPipelineApi.InspectLooseCookedRootAsync((scope.Output ?? throw new InvalidOperationException("The import has no generation owner.")).Path, cancellationToken).ConfigureAwait(false);
        var owned = scope.PreviousProvenance?.Products.FirstOrDefault(product => product.SourceUri == source.AssetUri)?.Outputs
            .Select(static output => output.Asset.VirtualPath).ToHashSet(StringComparer.OrdinalIgnoreCase) ?? [];
        var conflict = inspection.Assets.FirstOrDefault(asset => prefixes.Any(prefix => asset.VirtualPath.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) && !owned.Contains(asset.VirtualPath));
        return conflict is null ? null : Collision(conflict.VirtualPath);

        DiagnosticRecord Collision(string path) => new()
        {
            OperationId = operationId, Domain = FailureDomain.AssetImport, Severity = DiagnosticSeverity.Error,
            Code = AssetImportDiagnosticCodes.ImportFailed, AffectedPath = source.SourceAbsolutePath, AffectedVirtualPath = source.AssetUri.AbsolutePath,
            Message = $"Import of '{settings.Name}' overlaps '{path}'. Choose a different destination without replacing existing assets.",
        };
    }
}
