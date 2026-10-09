// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Keeps open material documents in step with asset relocations without closing them: a document re-points its
/// texture paths, and its own location when it moved, in memory; undo and redo resolve captured texture paths.
/// </summary>
public sealed partial class MaterialDocumentService
{
    /// <inheritdoc/>
    public bool FollowRelocation(Guid documentId, AssetRelocationChange change)
    {
        ArgumentNullException.ThrowIfNull(change);
        lock (this.sync)
        {
            if (!this.documents.TryGetValue(documentId, out var document))
            {
                return false;
            }

            var movedPath = change.MapPath(document.SourcePath);
            var rewrite = change.FindRewrite(document.SourcePath);
            if (movedPath is null && rewrite is null)
            {
                return false;
            }

            // The relocation runs only while the document is saved, so it equals its file before the rewrite.
            var source = Retarget(document.Source, change.MapReference);
            var materialUri = change.MapReference(document.MaterialUri) ?? document.MaterialUri;
            var sourcePath = movedPath ?? document.SourcePath;
            this.documents[documentId] = document with
            {
                MaterialUri = materialUri,
                SourcePath = sourcePath,
                DisplayName = movedPath is null ? document.DisplayName : RelocationPaths.GetDisplayName(Path.GetFileName(sourcePath)),
                Source = source,
                Asset = CreateAsset(materialUri, source),
            };
            this.histories[documentId].SavedSource = source;
            if (rewrite is not null)
            {
                this.fileVersions[documentId] = rewrite.Written;
            }

            var state = this.CreateCookDocumentState(this.documents[documentId]);
            if (movedPath is null)
            {
                this.cookRegistrations[documentId].UpdateState(state);
            }
            else
            {
                this.cookRegistrations[documentId].RelocateSource(state);
            }

            this.LogMaterialFollowedRelocation(document.MaterialUri, materialUri, rewrite is not null);
            return true;
        }
    }

    private static MaterialSource Retarget(MaterialSource source, Func<string, string?> map)
    {
        foreach (var (channel, path) in source.TextureReferences.ToArray())
        {
            if (map(path) is { } mapped)
            {
                source = source.WithTextureReference(channel, mapped);
            }
        }

        return source;
    }

    /// <summary>Resolves a history snapshot's texture paths captured before relocations to their current identities.</summary>
    /// <param name="documentId">The document.</param>
    /// <param name="source">The captured snapshot.</param>
    /// <param name="warn">Whether to warn about deleted textures: when the step applies, not when it is validated.</param>
    private MaterialSource RedirectCaptured(Guid documentId, MaterialSource source, bool warn)
    {
        if (this.redirects is null)
        {
            return source;
        }

        var resolved = Retarget(source, path =>
        {
            var current = this.redirects.Resolve(path);
            return string.Equals(current, path, StringComparison.Ordinal) ? null : current;
        });
        if (!warn || !this.documents.TryGetValue(documentId, out var document))
        {
            return resolved;
        }

        // Warn only for textures the step brings back; one the material already shows as missing was reported before.
        var current = document.Source.TextureReferences.Values.ToHashSet(StringComparer.OrdinalIgnoreCase);
        var deleted = resolved.TextureReferences.Values
            .Where(path => !current.Contains(path) && this.redirects.WasDeleted(new Uri(Oxygen.Managed.Core.AssetUris.Scheme + "://" + path)))
            .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        if (deleted.Length != 0)
        {
            var names = string.Join(", ", deleted.Select(Path.GetFileName));
            this.LogRestoredDeletedTexture(document.MaterialUri, names);
            this.PublishMaterialWarning(document, $"{names} was deleted, so the restored reference shows as missing until you choose another texture.");
        }

        return resolved;
    }

    private void PublishMaterialWarning(MaterialDocument document, string message)
    {
        if (this.operationResults is null)
        {
            return;
        }

        var operationId = Guid.NewGuid();
        var scope = new AffectedScope
        {
            DocumentId = document.DocumentId,
            DocumentPath = document.SourcePath,
            DocumentName = document.DisplayName,
            AssetId = document.MaterialUri.ToString(),
            AssetSourcePath = document.SourcePath,
            AssetVirtualPath = document.MaterialUri.AbsolutePath,
        };
        this.operationResults.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = MaterialOperationKinds.EditScalar,
            Status = OperationStatus.SucceededWithWarnings,
            Severity = DiagnosticSeverity.Warning,
            Title = "Restored a reference to a deleted texture",
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = scope,
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = operationId,
                    Domain = FailureDomain.MaterialAuthoring,
                    Severity = DiagnosticSeverity.Warning,
                    Code = "MATERIAL_RESTORED_DELETED_TEXTURE",
                    Message = message,
                    AffectedPath = document.SourcePath,
                    AffectedVirtualPath = document.MaterialUri.AbsolutePath,
                    AffectedEntity = scope,
                },
            ],
        });
    }
}
