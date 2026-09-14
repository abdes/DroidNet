// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Cooks retained models through the same snapshot, staging and publication path as authored descriptors.</summary>
public sealed partial class ContentPipelineService
{
    private static async Task ReleaseImportedDiscoveryAfterDrainAsync(Task drain, FileStream ownership)
    {
        try
        {
            await drain.ConfigureAwait(false);
        }
        finally
        {
            await ownership.DisposeAsync().ConfigureAwait(false);
        }
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

        if (previous is not null)
        {
            var oldEntries = scope.PreviousProvenance!.Roots.Where(root => string.Equals(root.Mount, source.MountName, StringComparison.Ordinal))
                .SelectMany(static root => root.Assets).ToDictionary(static asset => asset.Entry.VirtualPath, StringComparer.Ordinal);
            var newEntries = result.Inspection!.Assets.ToDictionary(static asset => asset.VirtualPath, StringComparer.Ordinal);
            foreach (var output in previous.Outputs)
            {
                if (oldEntries.TryGetValue(output.Asset.VirtualPath, out var old) && old.Entry.AssetKey is { } key
                    && !string.Equals(key, newEntries[output.Asset.VirtualPath].AssetKey, StringComparison.Ordinal))
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

    private async Task<DiscoveredSceneSource> DiscoverChangedImportedSourceAsync(ContentCookOperation operation, ContentCookInput input, NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        var inspector = this.engineContentPipelineApi as ISceneSourceInspector
            ?? throw new InvalidOperationException("The native pipeline cannot inspect changed model sources.");
        var ownership = CookOutputLease.AcquireOperation(operation.Project.ProjectRoot, operation.OperationId);
        Task? retainedDrain = null;
        try
        {
            return await new SceneImportSourceDiscovery(cookDocuments, this.cookCoordinator, inspector)
                .DiscoverAsync(operation, input.SourceAbsolutePath, cancellationToken, artifacts).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException failure)
        {
            retainedDrain = ReleaseImportedDiscoveryAfterDrainAsync(failure.DrainCompletion, ownership);
            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, retainedDrain);
        }
        finally
        {
            if (retainedDrain is null)
            {
                await ownership.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private async Task<ContentCookResult> CookWithImportedSourcesAsync(Guid operationId, ContentCookScope scope, CancellationToken cancellationToken)
    {
        var results = new List<ContentCookResult>();
        foreach (var input in scope.Inputs.Where(static input => input.Kind == ContentCookAssetKind.ForeignSource))
        {
            var settings = NativeSceneImportSettings.Parse(await File.ReadAllBytesAsync(input.SourceAbsolutePath + NativeSceneImportSettings.SidecarSuffix, cancellationToken).ConfigureAwait(false));
            var collision = await this.FindImportedOutputCollisionAsync(operationId, scope, input, settings, cancellationToken).ConfigureAwait(false);
            if (collision is not null)
            {
                results.Add(new(operationId, scope.TargetKind, OperationStatus.Failed, [collision], [], null, null));
                continue;
            }

            var sourceScope = scope with { Inputs = [input] };
            var manifest = new ContentImportManifest(
                1,
                scope.StagingOutputRoot!,
                new ContentImportLayout("/" + input.MountName) { DescriptorsDirectory = settings.OutputDirectory },
                [
                    new ContentImportJob(
                        "model",
                        Path.GetExtension(input.SourceRelativePath).Equals(".fbx", StringComparison.OrdinalIgnoreCase) ? "fbx" : "gltf",
                        input.SourceRelativePath,
                        [],
                        Output: null,
                        settings.Name)
                    {
                        ContentPolicy = settings.ContentPolicy,
                        UnitPolicy = settings.UnitPolicy,
                        BakeTransforms = settings.BakeTransforms,
                        NormalsPolicy = settings.NormalsPolicy,
                        TangentsPolicy = settings.TangentsPolicy,
                    },
                ]);
            var result = await this.ExecuteManifestAsync(operationId, scope.TargetKind, sourceScope, manifest, [], cancellationToken).ConfigureAwait(false);
            results.Add(ValidateImportedIdentities(operationId, scope, input, result));
        }

        var authored = scope.Inputs.Where(static input => input.Kind != ContentCookAssetKind.ForeignSource).ToArray();
        if (authored.Length != 0)
        {
            results.Add(await this.CookMixedInputsAsync(operationId, scope with { Inputs = authored }, cancellationToken).ConfigureAwait(false));
        }

        var merged = MergeProjectResults(operationId, results) with { TargetKind = scope.TargetKind };
        return merged with { VerifiedRoot = results[^1].VerifiedRoot, Inspection = results[^1].Inspection, Validation = results[^1].Validation };
    }

    private async Task<DiagnosticRecord?> FindImportedOutputCollisionAsync(Guid operationId, ContentCookScope scope, ContentCookInput source, NativeSceneImportSettings settings, CancellationToken cancellationToken)
    {
        var prefix = settings.OutputPrefix;
        var conflictingInput = scope.Inputs.FirstOrDefault(input => input.AssetUri != source.AssetUri
            && input.OutputVirtualPath is { } output && (output.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)
                || (input.Kind == ContentCookAssetKind.ForeignSource && prefix.StartsWith(output, StringComparison.OrdinalIgnoreCase))));
        if (conflictingInput is not null)
        {
            return Collision(conflictingInput.AssetUri.ToString());
        }

        if (!File.Exists(Path.Combine(scope.StagingOutputRoot!, "container.index.bin")))
        {
            return null;
        }

        var inspection = await this.engineContentPipelineApi.InspectLooseCookedRootAsync(scope.StagingOutputRoot!, cancellationToken).ConfigureAwait(false);
        var owned = scope.PreviousProvenance?.Products.FirstOrDefault(product => product.SourceUri == source.AssetUri)?.Outputs
            .Select(static output => output.Asset.VirtualPath).ToHashSet(StringComparer.OrdinalIgnoreCase) ?? [];
        var conflict = inspection.Assets.FirstOrDefault(asset => asset.VirtualPath.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) && !owned.Contains(asset.VirtualPath));
        return conflict is null ? null : Collision(conflict.VirtualPath);

        DiagnosticRecord Collision(string path) => new()
        {
            OperationId = operationId, Domain = FailureDomain.AssetImport, Severity = DiagnosticSeverity.Error,
            Code = AssetImportDiagnosticCodes.ImportFailed, AffectedPath = source.SourceAbsolutePath, AffectedVirtualPath = source.AssetUri.AbsolutePath,
            Message = $"Import destination '{prefix}' overlaps '{path}'. Choose a different destination without replacing existing assets.",
        };
    }
}
