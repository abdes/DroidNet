// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Reconciles abandoned publications before cooking or mounting a project.</summary>
public sealed partial class CookPublicationService
{
    /// <summary>Recovers and acquires the read ownership transferred to a newly mounted runtime.</summary>
    /// <param name="project">The project being activated.</param>
    /// <param name="cancellationToken">Cancels activation before native ownership is transferred.</param>
    /// <returns>A read lease covering verification and native mounting.</returns>
    public async Task<IDisposable> AcquireForMountAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        for (var attempt = 0; attempt < 3; ++attempt)
        {
            await this.RecoverAsync(project, verifyCommitted: false, cancellationToken).ConfigureAwait(false);
            var reader = CookOutputLease.AcquireRead(project.ProjectRoot);
            try
            {
                if (await this.HasAbandonedPublicationAsync(project, cancellationToken).ConfigureAwait(false))
                {
                    reader.Dispose();
                    continue;
                }

                await this.VerifyCurrentReceiptAsync(project, cancellationToken).ConfigureAwait(false);
                return reader;
            }
            catch
            {
                reader.Dispose();
                throw;
            }
        }

        throw new IOException("Publication changed during project activation. Retry opening the project.");
    }

    /// <summary>Restores interrupted publications and validates the current committed generation before mounting.</summary>
    /// <param name="project">The project being activated.</param>
    /// <param name="cancellationToken">Cancels before filesystem replacement.</param>
    /// <returns>Completion of recovery and committed-output verification.</returns>
    public Task RecoverBeforeMountAsync(ProjectContext project, CancellationToken cancellationToken)
        => this.RecoverAsync(project, verifyCommitted: true, cancellationToken);

    /// <summary>Restores interrupted transactions while allowing a new cook to repair corrupt committed products.</summary>
    /// <param name="project">The project holding the cook writer.</param>
    /// <param name="cancellationToken">Cancels before filesystem replacement.</param>
    /// <returns>Completion of pending recovery.</returns>
    internal Task RecoverBeforeCookAsync(ProjectContext project, CancellationToken cancellationToken)
        => this.RecoverAsync(project, verifyCommitted: false, cancellationToken);

    /// <summary>Checks the receipt/cache pair without invalidating otherwise reusable product bytes.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>Whether publication metadata remains trustworthy.</returns>
    internal async Task<bool> HasCommittedMetadataAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        using var reader = CookOutputLease.AcquireRead(project.ProjectRoot);
        return await this.HasCommittedMetadataUnderLeaseAsync(project, cancellationToken).ConfigureAwait(false);
    }

    /// <summary>Verifies metadata while the caller already protects the published generation.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>Whether the receipt and committed journal agree.</returns>
    internal async Task<bool> HasCommittedMetadataUnderLeaseAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        try
        {
            var snapshot = await files.ReadAsync(Path.Combine(project.ProjectRoot, CookPublicationTransaction.PublicationMetadata), cancellationToken).ConfigureAwait(false);
            if (!snapshot.Version.Exists)
            {
                return false;
            }

            var receipt = JsonSerializer.Deserialize<CookPublicationReceipt>(snapshot.Content.AsSpan());
            if (receipt is null || receipt.Version != 1 || receipt.ProjectId != project.ProjectId)
            {
                return false;
            }

            var transaction = await CookPublicationTransaction.LoadReadOnlyAsync(project, receipt.OperationId, files, cancellationToken).ConfigureAwait(false);
            await transaction.VerifyCommittedMetadataAsync().ConfigureAwait(false);
            return true;
        }
        catch (Exception exception) when (exception is InvalidDataException or JsonException or FileNotFoundException or DirectoryNotFoundException)
        {
            return false;
        }
    }

    private static FileStream? TryAcquireOperation(ProjectContext project, Guid operationId)
    {
        try
        {
            return CookOutputLease.AcquireOperation(project.ProjectRoot, operationId);
        }
        catch (CookOutputBusyException)
        {
            return null;
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "A non-null operation lease is disposed by await using; null branches acquire no ownership.")]
    private async Task RecoverAsync(ProjectContext project, bool verifyCommitted, CancellationToken cancellationToken)
    {
        var root = Path.Combine(project.ProjectRoot, ".build", "cook");
        CookOutputLease.RejectReparsePoint(root);
        if (!Directory.Exists(root))
        {
            return;
        }

        foreach (var directory in Directory.EnumerateDirectories(root).Order(StringComparer.Ordinal))
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!Guid.TryParseExact(Path.GetFileName(directory), "N", out var operationId)
                || !File.Exists(Path.Combine(directory, "publication.json")))
            {
                continue;
            }

            var operation = TryAcquireOperation(project, operationId);
            if (operation is null)
            {
                continue;
            }

            await using var operationLifetime = operation.ConfigureAwait(false);
            var snapshot = await files.ReadAsync(Path.Combine(directory, "publication.json"), cancellationToken).ConfigureAwait(false);
            using var document = JsonDocument.Parse(snapshot.Content.AsMemory());
            var phase = (CookPublicationPhase)document.RootElement.GetProperty("phase").GetInt32();
            if (phase is CookPublicationPhase.Committed or CookPublicationPhase.RolledBack)
            {
                continue;
            }

            await this.RestoreOperationAsync(project, operationId, cancellationToken).ConfigureAwait(false);
        }

        if (verifyCommitted)
        {
            await this.VerifyCurrentReceiptAsync(project, cancellationToken).ConfigureAwait(false);
        }
    }

    private async Task RestoreOperationAsync(ProjectContext project, Guid operationId, CancellationToken cancellationToken)
    {
        var preview = await this.CapturePreviewAsync(project).ConfigureAwait(false);
        try
        {
            if (preview is not null)
            {
                await preview.PrepareReplacementAsync().ConfigureAwait(false);
            }

            using var writer = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, cancellationToken).ConfigureAwait(false);
            var transaction = await CookPublicationTransaction.LoadAsync(project, operationId, files, writer, cancellationToken).ConfigureAwait(false);
            await transaction.RecoverAsync(writer).ConfigureAwait(false);
            if (preview is not null)
            {
                var cooked = Path.Combine(project.ProjectRoot, ".cooked");
                var roots = Directory.Exists(cooked) ? Directory.EnumerateDirectories(cooked)
                    .Where(path => File.Exists(Path.Combine(path, "container.index.bin"))).ToArray() : [];
                await preview.MountAsync(roots, writer).ConfigureAwait(false);
                await preview.ResumeAsync().ConfigureAwait(false);
            }

            await transaction.CleanupAsync(writer).ConfigureAwait(false);
        }
        finally
        {
            if (preview is not null)
            {
                await preview.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private async Task VerifyCurrentReceiptAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        using var reader = CookOutputLease.AcquireRead(project.ProjectRoot);
        var receiptFile = await files.ReadAsync(Path.Combine(project.ProjectRoot, CookPublicationTransaction.PublicationMetadata), cancellationToken).ConfigureAwait(false);
        if (!receiptFile.Version.Exists)
        {
            return;
        }

        var receipt = JsonSerializer.Deserialize<CookPublicationReceipt>(receiptFile.Content.AsSpan())
            ?? throw new InvalidDataException("The publication receipt is empty.");
        if (receipt.Version != 1 || receipt.ProjectId != project.ProjectId || receipt.Roots.IsDefaultOrEmpty || receipt.Roots.Any(static root => root is null))
        {
            throw new InvalidDataException("The publication receipt does not belong to this project.");
        }

        _ = CookStagingArea.ValidateMounts(receipt.Roots.Select(static root => root.Mount));
        CookOutputLease.RejectReparsePoint(Path.Combine(project.ProjectRoot, ".cooked"));
        var current = await CookPublicationTransaction.LoadReadOnlyAsync(project, receipt.OperationId, files, cancellationToken).ConfigureAwait(false);
        await current.VerifyCommittedAsync().ConfigureAwait(false);

        foreach (var root in receipt.Roots)
        {
            if (root.SharedFiles.IsDefaultOrEmpty || root.Assets.IsDefault)
            {
                throw new InvalidDataException("The publication receipt contains incomplete root evidence.");
            }

            var image = await CookRootImage.CaptureAsync(Path.Combine(project.ProjectRoot, ".cooked", root.Mount), copyTo: null, cancellationToken).ConfigureAwait(false);
            foreach (var proof in root.SharedFiles.Concat(root.Assets.Select(static asset => asset.File)))
            {
                if (!image.Files.TryGetValue(proof.RelativePath, out var file) || file.Size != proof.Size
                    || !string.Equals(file.Sha256, proof.Sha256, StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidDataException($"Cooked content changed in '{root.Mount}'. Cook the affected content before mounting it.");
                }
            }
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "A non-null operation lease is disposed by await using; null branches acquire no ownership.")]
    private async Task<bool> HasAbandonedPublicationAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        var root = Path.Combine(project.ProjectRoot, ".build", "cook");
        foreach (var directory in Directory.EnumerateDirectories(root))
        {
            if (!Guid.TryParseExact(Path.GetFileName(directory), "N", out var operationId)
                || !File.Exists(Path.Combine(directory, "publication.json")))
            {
                continue;
            }

            var operation = TryAcquireOperation(project, operationId);
            if (operation is null)
            {
                continue;
            }

            await using var operationLifetime = operation.ConfigureAwait(false);
            var snapshot = await files.ReadAsync(Path.Combine(directory, "publication.json"), cancellationToken).ConfigureAwait(false);
            using var document = JsonDocument.Parse(snapshot.Content.AsMemory());
            var phase = (CookPublicationPhase)document.RootElement.GetProperty("phase").GetInt32();
            if (phase is not (CookPublicationPhase.Committed or CookPublicationPhase.RolledBack))
            {
                return true;
            }
        }

        return false;
    }
}
