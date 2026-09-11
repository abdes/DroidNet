// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Managed.Assets.Import.Materials;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Preserves document identity while resolving external save conflicts.</summary>
public sealed partial class MaterialDocumentService
{
    /// <inheritdoc/>
    public async Task<MaterialDocument> ReloadAsync(Guid documentId, CancellationToken cancellationToken = default)
    {
        SemaphoreSlim gate;
        lock (this.sync)
        {
            this.FinishMaterialGesture(documentId, commit: false);
            gate = this.saveGates[documentId];
        }

        await gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var original = this.GetDocument(documentId);
            var snapshot = await this.atomicFiles.ReadAsync(original.SourcePath, cancellationToken).ConfigureAwait(false);
            if (!snapshot.Version.Exists)
            {
                throw new FileNotFoundException("The external material source no longer exists.", original.SourcePath);
            }

            var source = WithName(MaterialSourceReader.Read(snapshot.Content.ToArray()), original.DisplayName);
            lock (this.sync)
            {
                var current = this.GetDocument(documentId);
                if (current.Revision != original.Revision || this.histories[documentId].Active is not null)
                {
                    throw new InvalidOperationException("The material changed while reloading. Confirm reload again to discard those newer edits.");
                }

                var revision = current.Revision + 1;
                var reloaded = current with
                {
                    Source = source,
                    Asset = CreateAsset(current.MaterialUri, source),
                    Revision = revision,
                    SavedRevision = revision,
                    IsDirty = false,
                    CookState = MaterialCookState.Stale,
                };
                this.fileVersions[documentId] = snapshot.Version;
                this.histories[documentId].Keeper.Clear();
                this.histories[documentId].SavedSource = source;
                this.documents[documentId] = reloaded;
                return reloaded;
            }
        }
        finally
        {
            _ = gate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<Uri> SaveCopyAsync(Guid documentId, Uri targetUri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(targetUri);
        var target = this.pathResolver.Resolve(targetUri);
        byte[] bytes;
        lock (this.sync)
        {
            this.FinishMaterialGesture(documentId, commit: true);
            var original = this.GetDocument(documentId);
            if (string.Equals(target.SourcePath, original.SourcePath, StringComparison.OrdinalIgnoreCase))
            {
                throw new ArgumentException("A copy must use a different authored target.", nameof(targetUri));
            }

            var source = WithName(original.Source, GetMaterialDisplayName(target.MaterialUri));
            if (this.ValidateSave(original, source) is { } failure)
            {
                throw new InvalidOperationException($"The material copy did not pass validation ({failure.OperationId}).");
            }

            bytes = SerializeSource(source);
        }

        _ = await this.atomicFiles.WriteAsync(target.SourcePath, bytes, FileVersion.Missing, cancellationToken).ConfigureAwait(false);
        return target.MaterialUri;
    }
}
