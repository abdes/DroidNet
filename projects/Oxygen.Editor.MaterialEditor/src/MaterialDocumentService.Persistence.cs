// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Managed.Assets.Import.Materials;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Publishes immutable material snapshots and preserves the original failure diagnostics.</summary>
public sealed partial class MaterialDocumentService
{
    private async Task<MaterialSaveResult?> PersistMaterialSnapshotAsync(MaterialDocument document, MaterialSource source, CancellationToken cancellationToken)
    {
        try
        {
            _ = await this.sourceWrites.RunAsync(
                document.SourcePath,
                async () =>
                {
                    FileVersion baseline;
                    lock (this.sync)
                    {
                        baseline = this.fileVersions[document.DocumentId];
                    }

                    var version = await this.atomicFiles.WriteAsync(document.SourcePath, SerializeSource(source), baseline, cancellationToken).ConfigureAwait(false);
                    lock (this.sync)
                    {
                        this.fileVersions[document.DocumentId] = version;
                    }

                    return true;
                },
                cancellationToken).ConfigureAwait(false);
            return null;
        }
        catch (StorageWriteConflictException exception)
        {
            return this.ReportSaveFailure(document, exception, "Conflict", "Material changed outside this document") with { IsConflict = true };
        }
        catch (OperationCanceledException exception)
        {
            return this.ReportSaveFailure(document, exception, "SaveCancelled", "Material save cancelled");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return this.ReportSaveFailure(document, exception, "MATERIAL.SaveFailed", "Material was not saved");
        }
    }

    private MaterialSaveResult ReportSaveFailure(MaterialDocument document, Exception exception, string code, string summary)
    {
        var operationId = this.PublishMaterialFailure(MaterialOperationKinds.Save, document, DiagnosticCodes.DocumentPrefix + code, summary, exception.Message, FailureDomain.Document);
        return new(Succeeded: false, operationId) { HasUnsavedChanges = this.GetDocument(document.DocumentId).IsDirty };
    }
}
